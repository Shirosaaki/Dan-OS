//
// UHCI (USB 1.1) Controller Driver Implementation
//

#include <kernel/drivers/uhci.h>
#include <kernel/drivers/pci.h>
#include <kernel/drivers/usb.h>
#include <kernel/sys/tty.h>
#include <kernel/sys/kmalloc.h>
#include <cpu/ports.h>
// Forward declarations
static int uhci_init_controller(usb_controller_t* ctrl);
static void uhci_shutdown(usb_controller_t* ctrl);
static int uhci_reset_port(usb_controller_t* ctrl, int port);
static int uhci_control_transfer(usb_device_t* dev, usb_setup_packet_t* setup, void* data, uint16_t length);
static int uhci_bulk_transfer(usb_device_t* dev, uint8_t endpoint, void* data, uint32_t length);
static int uhci_interrupt_transfer(usb_device_t* dev, uint8_t endpoint, void* data, uint32_t length);
static void uhci_poll(usb_controller_t* ctrl);

// UHCI operations
usb_controller_ops_t uhci_ops = {
    .init = uhci_init_controller,
    .shutdown = uhci_shutdown,
    .reset_port = uhci_reset_port,
    .control_transfer = uhci_control_transfer,
    .bulk_transfer = uhci_bulk_transfer,
    .interrupt_transfer = uhci_interrupt_transfer,
    .poll = uhci_poll
};

// Small delay
static void uhci_delay(int ms) {
    for (volatile int i = 0; i < ms * 10000; i++) {
        __asm__ volatile("nop");
    }
}

// Read UHCI register
static uint16_t uhci_read16(uhci_controller_t* uhci, uint16_t reg) {
    return inw(uhci->io_base + reg);
}

// Write UHCI register
static void uhci_write16(uhci_controller_t* uhci, uint16_t reg, uint16_t value) {
    outw(uhci->io_base + reg, value);
}

static uint32_t uhci_read32(uhci_controller_t* uhci, uint16_t reg) {
    return inl(uhci->io_base + reg);
}

static void uhci_write32(uhci_controller_t* uhci, uint16_t reg, uint32_t value) {
    outl(uhci->io_base + reg, value);
}

// Allocate a TD from the pool
static uhci_td_t* uhci_alloc_td(uhci_controller_t* uhci) {
    for (int i = 0; i < uhci->td_pool_size; i++) {
        if (!(uhci->td_pool_bitmap[i / 8] & (1 << (i % 8)))) {
            uhci->td_pool_bitmap[i / 8] |= (1 << (i % 8));
            uhci_td_t* td = &uhci->td_pool[i];
            // Clear TD
            td->link_ptr = UHCI_TD_LINK_TERMINATE;
            td->ctrl_status = 0;
            td->token = 0;
            td->buffer_ptr = 0;
            return td;
        }
    }
    return 0;
}

// Free a TD back to pool
static void uhci_free_td(uhci_controller_t* uhci, uhci_td_t* td) {
    int index = td - uhci->td_pool;
    if (index >= 0 && index < uhci->td_pool_size) {
        uhci->td_pool_bitmap[index / 8] &= ~(1 << (index % 8));
    }
}

// Get physical address of TD
static uint32_t uhci_td_phys(uhci_controller_t* uhci, uhci_td_t* td) {
    // In our simple implementation, virtual = physical for kernel memory
    return (uint32_t)(uintptr_t)td;
}

// Reset the UHCI controller
static int uhci_reset(uhci_controller_t* uhci) {
    // Global reset
    uhci_write16(uhci, UHCI_USBCMD, UHCI_CMD_GRESET);
    uhci_delay(50);
    uhci_write16(uhci, UHCI_USBCMD, 0);
    uhci_delay(10);
    
    // Host controller reset
    uhci_write16(uhci, UHCI_USBCMD, UHCI_CMD_HCRESET);
    
    // Wait for reset to complete
    for (int i = 0; i < 100; i++) {
        uhci_delay(1);
        if (!(uhci_read16(uhci, UHCI_USBCMD) & UHCI_CMD_HCRESET)) {
            return 0;
        }
    }
    
    return -1; // Reset timeout
}

// Initialize UHCI controller
int uhci_init(usb_controller_t* ctrl, pci_device_t* pci_dev) {
    // Allocate UHCI-specific data
    uhci_controller_t* uhci = (uhci_controller_t*)kmalloc(sizeof(uhci_controller_t));
    if (!uhci) return -1;
    
    // Initialize structure
    for (int i = 0; i < (int)sizeof(uhci_controller_t); i++) {
        ((uint8_t*)uhci)[i] = 0;
    }
    
    uhci->pci_dev = pci_dev;
    
    // Get I/O base from BAR4 (UHCI uses I/O space)
    uint32_t bar4 = pci_dev->bar[4];
    if (!(bar4 & 0x01)) {
        // Not I/O space - try BAR0
        bar4 = pci_dev->bar[0];
        if (!(bar4 & 0x01)) {
            tty_putstr("UHCI: No I/O BAR found\n");
            kfree(uhci);
            return -1;
        }
    }
    uhci->io_base = bar4 & 0xFFFC;
    
    tty_putstr("UHCI: I/O base = 0x");
    tty_puthex(uhci->io_base);
    tty_putstr("\n");
    
    // Enable I/O space and bus mastering
    pci_enable_io_space(pci_dev);
    pci_enable_bus_mastering(pci_dev);
    
    // Reset controller
    if (uhci_reset(uhci) != 0) {
        tty_putstr("UHCI: Reset failed\n");
        kfree(uhci);
        return -1;
    }
    
    // Allocate frame list (1024 entries, must be 4KB aligned)
    uhci->frame_list = (uint32_t*)kmalloc_aligned(4096, 4096);
    if (!uhci->frame_list) {
        tty_putstr("UHCI: Failed to allocate frame list\n");
        kfree(uhci);
        return -1;
    }
    
    // Allocate TD pool (128 TDs)
    uhci->td_pool_size = 128;
    uhci->td_pool = (uhci_td_t*)kmalloc_aligned(uhci->td_pool_size * sizeof(uhci_td_t), 16);
    uhci->td_pool_bitmap = (uint8_t*)kmalloc((uhci->td_pool_size + 7) / 8);
    if (!uhci->td_pool || !uhci->td_pool_bitmap) {
        tty_putstr("UHCI: Failed to allocate TD pool\n");
        kfree(uhci);
        return -1;
    }
    
    // Clear TD pool bitmap
    for (int i = 0; i < (uhci->td_pool_size + 7) / 8; i++) {
        uhci->td_pool_bitmap[i] = 0;
    }
    
    // Allocate queue heads
    uhci->qh_control = (uhci_qh_t*)kmalloc_aligned(sizeof(uhci_qh_t), 16);
    uhci->qh_bulk = (uhci_qh_t*)kmalloc_aligned(sizeof(uhci_qh_t), 16);
    
    if (!uhci->qh_control || !uhci->qh_bulk) {
        tty_putstr("UHCI: Failed to allocate queue heads\n");
        kfree(uhci);
        return -1;
    }
    
    // Initialize queue heads
    uhci->qh_control->head_link_ptr = (uint32_t)(uintptr_t)uhci->qh_bulk | UHCI_QH_LINK_QH;
    uhci->qh_control->element_link_ptr = UHCI_QH_LINK_TERMINATE;
    
    uhci->qh_bulk->head_link_ptr = UHCI_QH_LINK_TERMINATE;
    uhci->qh_bulk->element_link_ptr = UHCI_QH_LINK_TERMINATE;
    
    // Initialize frame list - all point to control QH
    for (int i = 0; i < 1024; i++) {
        uhci->frame_list[i] = (uint32_t)(uintptr_t)uhci->qh_control | UHCI_QH_LINK_QH;
    }
    
    // Set frame list base address
    uhci_write32(uhci, UHCI_FRBASEADD, (uint32_t)(uintptr_t)uhci->frame_list);
    
    // Set frame number to 0
    uhci_write16(uhci, UHCI_FRNUM, 0);
    
    // Set SOF timing
    uhci_write16(uhci, UHCI_SOFMOD, 0x40);
    
    // Clear status
    uhci_write16(uhci, UHCI_USBSTS, 0xFFFF);
    
    // Enable interrupts
    uhci_write16(uhci, UHCI_USBINTR, UHCI_INTR_IOC | UHCI_INTR_SP);
    
    // Start controller
    uhci_write16(uhci, UHCI_USBCMD, UHCI_CMD_RS | UHCI_CMD_CF | UHCI_CMD_MAXP);
    
    // Store UHCI data in controller
    ctrl->driver_data = uhci;
    ctrl->io_base = uhci->io_base;
    ctrl->type = USB_CONTROLLER_UHCI;
    ctrl->ops = &uhci_ops;
    
    tty_putstr("UHCI: Controller initialized\n");
    
    return 0;
}

static int uhci_init_controller(usb_controller_t* ctrl) {
    // Already initialized by uhci_init
    return 0;
}

static void uhci_shutdown(usb_controller_t* ctrl) {
    uhci_controller_t* uhci = (uhci_controller_t*)ctrl->driver_data;
    if (!uhci) return;
    
    // Stop controller
    uhci_write16(uhci, UHCI_USBCMD, 0);
    
    // Free resources
    if (uhci->frame_list) kfree(uhci->frame_list);
    if (uhci->td_pool) kfree(uhci->td_pool);
    if (uhci->td_pool_bitmap) kfree(uhci->td_pool_bitmap);
    if (uhci->qh_control) kfree(uhci->qh_control);
    if (uhci->qh_bulk) kfree(uhci->qh_bulk);
    
    kfree(uhci);
    ctrl->driver_data = 0;
}

// Reset a USB port
static int uhci_reset_port(usb_controller_t* ctrl, int port) {
    uhci_controller_t* uhci = (uhci_controller_t*)ctrl->driver_data;
    if (!uhci || port < 0 || port > 1) return -1;
    
    uint16_t port_reg = (port == 0) ? UHCI_PORTSC1 : UHCI_PORTSC2;
    
    // Assert port reset
    uhci_write16(uhci, port_reg, UHCI_PORT_PR);
    uhci_delay(50); // USB spec requires at least 10ms
    
    // Deassert port reset
    uhci_write16(uhci, port_reg, 0);
    uhci_delay(10);
    
    // Enable port
    for (int i = 0; i < 10; i++) {
        uint16_t status = uhci_read16(uhci, port_reg);
        
        // Clear change bits
        uhci_write16(uhci, port_reg, status | UHCI_PORT_CSC | UHCI_PORT_PEDC);
        
        if (status & UHCI_PORT_CCS) {
            // Device connected - enable port
            uhci_write16(uhci, port_reg, UHCI_PORT_PED);
            uhci_delay(10);
            
            status = uhci_read16(uhci, port_reg);
            if (status & UHCI_PORT_PED) {
                uhci->port_connected[port] = 1;
                uhci->port_enabled[port] = 1;
                uhci->port_low_speed[port] = (status & UHCI_PORT_LSDA) ? 1 : 0;
                
                tty_putstr("UHCI: Port ");
                tty_putdec(port);
                tty_putstr(" enabled (");
                tty_putstr(uhci->port_low_speed[port] ? "low" : "full");
                tty_putstr(" speed)\n");
                
                return 0;
            }
        }
        uhci_delay(10);
    }
    
    return -1;
}

// Build a TD for control transfer
static void uhci_build_td(uhci_td_t* td, uint8_t pid, uint8_t device, uint8_t endpoint,
                          uint8_t toggle, uint8_t low_speed, void* buffer, uint16_t length) {
    td->ctrl_status = UHCI_TD_STATUS_ACTIVE | (3 << 27); // Active, 3 error retries
    if (low_speed) {
        td->ctrl_status |= UHCI_TD_STATUS_LS;
    }
    
    // Max length is actual length - 1 (0 means 1 byte, 0x7FF means 2048 bytes)
    uint16_t max_len = (length > 0) ? (length - 1) : 0x7FF;
    
    td->token = (max_len << 21) | (toggle << 19) | (endpoint << 15) | 
                (device << 8) | pid;
    
    td->buffer_ptr = (uint32_t)(uintptr_t)buffer;
}

// Wait for TD to complete
static int uhci_wait_td(uhci_controller_t* uhci, uhci_td_t* td) {
    for (int i = 0; i < 1000; i++) {
        uint32_t status = td->ctrl_status;
        
        if (!(status & UHCI_TD_STATUS_ACTIVE)) {
            // TD completed
            if (status & UHCI_TD_STATUS_STALLED) {
                return USB_TRANSFER_STALL;
            }
            if (status & (UHCI_TD_STATUS_BABBLE | UHCI_TD_STATUS_CRC_TIMEOUT | 
                         UHCI_TD_STATUS_DATABUFFER | UHCI_TD_STATUS_BITSTUFF)) {
                return USB_TRANSFER_ERROR;
            }
            return USB_TRANSFER_SUCCESS;
        }
        
        uhci_delay(1);
    }
    
    return USB_TRANSFER_TIMEOUT;
}

// Perform control transfer
static int uhci_control_transfer(usb_device_t* dev, usb_setup_packet_t* setup, void* data, uint16_t length) {
    usb_controller_t* ctrl = dev->controller;
    uhci_controller_t* uhci = (uhci_controller_t*)ctrl->driver_data;
    
    int low_speed = (dev->speed == USB_SPEED_LOW);
    
    // Allocate TDs
    uhci_td_t* td_setup = uhci_alloc_td(uhci);
    uhci_td_t* td_data = 0;
    uhci_td_t* td_status = uhci_alloc_td(uhci);
    
    if (!td_setup || !td_status) {
        if (td_setup) uhci_free_td(uhci, td_setup);
        if (td_status) uhci_free_td(uhci, td_status);
        return USB_TRANSFER_ERROR;
    }
    
    // Build setup TD
    uhci_build_td(td_setup, UHCI_TD_TOKEN_PID_SETUP, dev->address, 0, 0, low_speed,
                  setup, 8);
    
    uhci_td_t* prev_td = td_setup;
    uint8_t toggle = 1;
    
    // Build data TDs if needed
    if (length > 0 && data) {
        uint8_t* data_ptr = (uint8_t*)data;
        uint16_t remaining = length;
        uint8_t pid = (setup->bmRequestType & USB_REQ_DIR_IN) ? 
                      UHCI_TD_TOKEN_PID_IN : UHCI_TD_TOKEN_PID_OUT;
        
        while (remaining > 0) {
            uint16_t chunk = (remaining > dev->max_packet_size) ? 
                            dev->max_packet_size : remaining;
            
            td_data = uhci_alloc_td(uhci);
            if (!td_data) {
                // Cleanup and return error
                return USB_TRANSFER_ERROR;
            }
            
            uhci_build_td(td_data, pid, dev->address, 0, toggle, low_speed,
                          data_ptr, chunk);
            
            // Link to previous TD
            prev_td->link_ptr = uhci_td_phys(uhci, td_data) | UHCI_TD_LINK_DEPTH;
            prev_td = td_data;
            
            data_ptr += chunk;
            remaining -= chunk;
            toggle ^= 1;
        }
    }
    
    // Build status TD (opposite direction of data)
    uint8_t status_pid = (setup->bmRequestType & USB_REQ_DIR_IN) ? 
                         UHCI_TD_TOKEN_PID_OUT : UHCI_TD_TOKEN_PID_IN;
    uhci_build_td(td_status, status_pid, dev->address, 0, 1, low_speed, 0, 0);
    td_status->ctrl_status |= UHCI_TD_STATUS_IOC; // Interrupt on complete
    
    // Link status TD
    prev_td->link_ptr = uhci_td_phys(uhci, td_status) | UHCI_TD_LINK_DEPTH;
    td_status->link_ptr = UHCI_TD_LINK_TERMINATE;
    
    // Insert into control queue
    uhci->qh_control->element_link_ptr = uhci_td_phys(uhci, td_setup);
    
    // Wait for completion
    int result = uhci_wait_td(uhci, td_status);
    
    // Remove from queue
    uhci->qh_control->element_link_ptr = UHCI_QH_LINK_TERMINATE;
    
    // Free TDs (simplified - should track all allocated TDs)
    uhci_free_td(uhci, td_setup);
    if (td_data) uhci_free_td(uhci, td_data);
    uhci_free_td(uhci, td_status);
    
    return result;
}

// Bulk transfer (simplified)
static int uhci_bulk_transfer(usb_device_t* dev, uint8_t endpoint, void* data, uint32_t length) {
    usb_controller_t* ctrl = dev->controller;
    uhci_controller_t* uhci = (uhci_controller_t*)ctrl->driver_data;
    
    int direction = (endpoint & USB_ENDPOINT_DIR_IN) ? 1 : 0;
    uint8_t ep_num = endpoint & 0x0F;
    uint8_t pid = direction ? UHCI_TD_TOKEN_PID_IN : UHCI_TD_TOKEN_PID_OUT;
    
    // Get endpoint toggle
    uint8_t toggle = dev->endpoints[ep_num].toggle;
    
    uint8_t* data_ptr = (uint8_t*)data;
    uint32_t remaining = length;
    int result = USB_TRANSFER_SUCCESS;
    
    while (remaining > 0 && result == USB_TRANSFER_SUCCESS) {
        uint16_t chunk = (remaining > dev->max_packet_size) ? 
                        dev->max_packet_size : remaining;
        
        uhci_td_t* td = uhci_alloc_td(uhci);
        if (!td) return USB_TRANSFER_ERROR;
        
        uhci_build_td(td, pid, dev->address, ep_num, toggle, 0, data_ptr, chunk);
        td->ctrl_status |= UHCI_TD_STATUS_IOC;
        
        // Insert into bulk queue
        uhci->qh_bulk->element_link_ptr = uhci_td_phys(uhci, td);
        
        result = uhci_wait_td(uhci, td);
        
        uhci->qh_bulk->element_link_ptr = UHCI_QH_LINK_TERMINATE;
        uhci_free_td(uhci, td);
        
        data_ptr += chunk;
        remaining -= chunk;
        toggle ^= 1;
    }
    
    // Save toggle state
    dev->endpoints[ep_num].toggle = toggle;
    
    return result;
}

// Interrupt transfer (polling mode)
static int uhci_interrupt_transfer(usb_device_t* dev, uint8_t endpoint, void* data, uint32_t length) {
    usb_controller_t* ctrl = dev->controller;
    uhci_controller_t* uhci = (uhci_controller_t*)ctrl->driver_data;
    
    int direction = (endpoint & USB_ENDPOINT_DIR_IN) ? 1 : 0;
    uint8_t ep_num = endpoint & 0x0F;
    uint8_t pid = direction ? UHCI_TD_TOKEN_PID_IN : UHCI_TD_TOKEN_PID_OUT;
    int low_speed = (dev->speed == USB_SPEED_LOW);
    
    uint8_t toggle = dev->endpoints[ep_num].toggle;
    
    uhci_td_t* td = uhci_alloc_td(uhci);
    if (!td) return USB_TRANSFER_ERROR;
    
    uhci_build_td(td, pid, dev->address, ep_num, toggle, low_speed, data, length);
    td->ctrl_status |= UHCI_TD_STATUS_IOC;
    
    // Insert directly into frame list for interrupt transfer
    uint32_t frame = uhci_read16(uhci, UHCI_FRNUM) & 0x3FF;
    uint32_t old_entry = uhci->frame_list[frame];
    uhci->frame_list[frame] = uhci_td_phys(uhci, td);
    
    int result = uhci_wait_td(uhci, td);
    
    // Restore frame list entry
    uhci->frame_list[frame] = old_entry;
    uhci_free_td(uhci, td);
    
    if (result == USB_TRANSFER_SUCCESS) {
        dev->endpoints[ep_num].toggle ^= 1;
    }
    
    return result;
}

// Poll for port status changes
static void uhci_poll(usb_controller_t* ctrl) {
    uhci_controller_t* uhci = (uhci_controller_t*)ctrl->driver_data;
    if (!uhci) return;
    
    // Check each port
    for (int port = 0; port < 2; port++) {
        uint16_t port_reg = (port == 0) ? UHCI_PORTSC1 : UHCI_PORTSC2;
        uint16_t status = uhci_read16(uhci, port_reg);
        
        // Check for connect status change
        if (status & UHCI_PORT_CSC) {
            // Clear the change bit
            uhci_write16(uhci, port_reg, status | UHCI_PORT_CSC);
            
            if (status & UHCI_PORT_CCS) {
                // Device connected
                tty_putstr("UHCI: Device connected on port ");
                tty_putdec(port);
                tty_putstr("\n");
                
                // Reset and enable the port
                uhci_reset_port(ctrl, port);
            } else {
                // Device disconnected
                tty_putstr("UHCI: Device disconnected from port ");
                tty_putdec(port);
                tty_putstr("\n");
                
                uhci->port_connected[port] = 0;
                uhci->port_enabled[port] = 0;
                uhci->port_device[port] = 0;
            }
        }
    }
    
    // Clear any pending USB interrupts
    uint16_t usb_status = uhci_read16(uhci, UHCI_USBSTS);
    if (usb_status & 0x1F) {
        uhci_write16(uhci, UHCI_USBSTS, usb_status & 0x1F);
    }
}

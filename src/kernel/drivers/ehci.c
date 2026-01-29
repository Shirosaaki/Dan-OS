//
// EHCI (USB 2.0) Controller Driver Implementation
//

#include "../includes/ehci.h"
#include "../includes/pci.h"
#include "../includes/usb.h"
#include "../includes/tty.h"
#include "../includes/kmalloc.h"

// Forward declarations
static int ehci_init_controller(usb_controller_t* ctrl);
static void ehci_shutdown(usb_controller_t* ctrl);
static int ehci_reset_port(usb_controller_t* ctrl, int port);
static int ehci_control_transfer(usb_device_t* dev, usb_setup_packet_t* setup, void* data, uint16_t length);
static int ehci_bulk_transfer(usb_device_t* dev, uint8_t endpoint, void* data, uint32_t length);
static int ehci_interrupt_transfer(usb_device_t* dev, uint8_t endpoint, void* data, uint32_t length);
static void ehci_poll(usb_controller_t* ctrl);

// EHCI operations
usb_controller_ops_t ehci_ops = {
    .init = ehci_init_controller,
    .shutdown = ehci_shutdown,
    .reset_port = ehci_reset_port,
    .control_transfer = ehci_control_transfer,
    .bulk_transfer = ehci_bulk_transfer,
    .interrupt_transfer = ehci_interrupt_transfer,
    .poll = ehci_poll
};

// Small delay
static void ehci_delay(int ms) {
    for (volatile int i = 0; i < ms * 10000; i++) {
        __asm__ volatile("nop");
    }
}

// Read EHCI capability register
static uint32_t ehci_cap_read32(ehci_controller_t* ehci, uint32_t offset) {
    return *(volatile uint32_t*)(ehci->mmio_base + offset);
}

static uint8_t ehci_cap_read8(ehci_controller_t* ehci, uint32_t offset) {
    return *(volatile uint8_t*)(ehci->mmio_base + offset);
}

// Read/write EHCI operational register
static uint32_t ehci_op_read32(ehci_controller_t* ehci, uint32_t offset) {
    return *(volatile uint32_t*)(ehci->op_base + offset);
}

static void ehci_op_write32(ehci_controller_t* ehci, uint32_t offset, uint32_t value) {
    *(volatile uint32_t*)(ehci->op_base + offset) = value;
}

// Port register access
static uint32_t ehci_port_read(ehci_controller_t* ehci, int port) {
    return ehci_op_read32(ehci, EHCI_PORTSC + port * 4);
}

static void ehci_port_write(ehci_controller_t* ehci, int port, uint32_t value) {
    ehci_op_write32(ehci, EHCI_PORTSC + port * 4, value);
}

// Allocate a qTD from the pool
static ehci_qtd_t* ehci_alloc_qtd(ehci_controller_t* ehci) {
    for (int i = 0; i < ehci->qtd_pool_size; i++) {
        if (!(ehci->qtd_pool_bitmap[i / 8] & (1 << (i % 8)))) {
            ehci->qtd_pool_bitmap[i / 8] |= (1 << (i % 8));
            ehci_qtd_t* qtd = &ehci->qtd_pool[i];
            // Clear qTD
            for (int j = 0; j < (int)(sizeof(ehci_qtd_t) / 4); j++) {
                ((uint32_t*)qtd)[j] = 0;
            }
            qtd->next_qtd = EHCI_QH_TERMINATE;
            qtd->alt_next_qtd = EHCI_QH_TERMINATE;
            return qtd;
        }
    }
    return 0;
}

// Free a qTD
static void ehci_free_qtd(ehci_controller_t* ehci, ehci_qtd_t* qtd) {
    int index = qtd - ehci->qtd_pool;
    if (index >= 0 && index < ehci->qtd_pool_size) {
        ehci->qtd_pool_bitmap[index / 8] &= ~(1 << (index % 8));
    }
}

// Allocate a QH from the pool
static ehci_qh_t* ehci_alloc_qh(ehci_controller_t* ehci) {
    for (int i = 0; i < ehci->qh_pool_size; i++) {
        if (!(ehci->qh_pool_bitmap[i / 8] & (1 << (i % 8)))) {
            ehci->qh_pool_bitmap[i / 8] |= (1 << (i % 8));
            ehci_qh_t* qh = &ehci->qh_pool[i];
            // Clear QH
            for (int j = 0; j < (int)(sizeof(ehci_qh_t) / 4); j++) {
                ((uint32_t*)qh)[j] = 0;
            }
            return qh;
        }
    }
    return 0;
}

// Free a QH
static void ehci_free_qh(ehci_controller_t* ehci, ehci_qh_t* qh) {
    int index = qh - ehci->qh_pool;
    if (index >= 0 && index < ehci->qh_pool_size) {
        ehci->qh_pool_bitmap[index / 8] &= ~(1 << (index % 8));
    }
}

// Get physical address
static uint32_t ehci_phys(void* ptr) {
    return (uint32_t)(uintptr_t)ptr;
}

// Reset EHCI controller
static int ehci_reset(ehci_controller_t* ehci) {
    // Stop controller
    uint32_t cmd = ehci_op_read32(ehci, EHCI_USBCMD);
    cmd &= ~EHCI_CMD_RS;
    ehci_op_write32(ehci, EHCI_USBCMD, cmd);
    
    // Wait for halt
    for (int i = 0; i < 100; i++) {
        if (ehci_op_read32(ehci, EHCI_USBSTS) & EHCI_STS_HCH) {
            break;
        }
        ehci_delay(1);
    }
    
    // Reset controller
    ehci_op_write32(ehci, EHCI_USBCMD, EHCI_CMD_HCRESET);
    
    // Wait for reset to complete
    for (int i = 0; i < 100; i++) {
        if (!(ehci_op_read32(ehci, EHCI_USBCMD) & EHCI_CMD_HCRESET)) {
            return 0;
        }
        ehci_delay(1);
    }
    
    return -1;
}

// Take ownership from BIOS
static void ehci_take_ownership(ehci_controller_t* ehci, pci_device_t* pci_dev) {
    // Check for EHCI Extended Capabilities
    uint32_t hccparams = ehci_cap_read32(ehci, EHCI_HCCPARAMS);
    uint8_t eecp = (hccparams >> 8) & 0xFF;
    
    while (eecp >= 0x40) {
        uint32_t cap = pci_read_dword(pci_dev->bus, pci_dev->device, 
                                       pci_dev->function, eecp);
        uint8_t cap_id = cap & 0xFF;
        
        if (cap_id == 1) {
            // EHCI Legacy Support capability
            // Request ownership
            pci_write_byte(pci_dev->bus, pci_dev->device, pci_dev->function,
                          eecp + 3, 1); // Set OS Owned Semaphore
            
            // Wait for BIOS to release
            for (int i = 0; i < 100; i++) {
                cap = pci_read_dword(pci_dev->bus, pci_dev->device,
                                     pci_dev->function, eecp);
                if (!(cap & (1 << 16))) { // BIOS Owned Semaphore cleared
                    break;
                }
                ehci_delay(10);
            }
            
            // Disable SMI
            pci_write_dword(pci_dev->bus, pci_dev->device, pci_dev->function,
                           eecp + 4, 0);
            break;
        }
        
        eecp = (cap >> 8) & 0xFF;
    }
}

// Initialize EHCI controller
int ehci_init(usb_controller_t* ctrl, pci_device_t* pci_dev) {
    // Allocate EHCI-specific data
    ehci_controller_t* ehci = (ehci_controller_t*)kmalloc(sizeof(ehci_controller_t));
    if (!ehci) return -1;
    
    // Clear structure
    for (int i = 0; i < (int)sizeof(ehci_controller_t); i++) {
        ((uint8_t*)ehci)[i] = 0;
    }
    
    ehci->pci_dev = pci_dev;
    
    // Get MMIO base from BAR0
    uint32_t bar0 = pci_dev->bar[0];
    if (bar0 & 0x01) {
        tty_putstr("EHCI: Expected memory BAR, got I/O BAR\n");
        kfree(ehci);
        return -1;
    }
    
    ehci->mmio_base = (volatile uint8_t*)(uintptr_t)(bar0 & 0xFFFFFFF0);
    
    tty_putstr("EHCI: MMIO base = 0x");
    tty_puthex((uint32_t)(uintptr_t)ehci->mmio_base);
    tty_putstr("\n");
    
    // Enable memory space and bus mastering
    pci_enable_memory_space(pci_dev);
    pci_enable_bus_mastering(pci_dev);
    
    // Read capability length
    ehci->cap_length = ehci_cap_read8(ehci, EHCI_CAPLENGTH);
    ehci->op_base = ehci->mmio_base + ehci->cap_length;
    
    // Get number of ports
    uint32_t hcsparams = ehci_cap_read32(ehci, EHCI_HCSPARAMS);
    ehci->num_ports = hcsparams & 0x0F;
    
    tty_putstr("EHCI: ");
    tty_putdec(ehci->num_ports);
    tty_putstr(" ports\n");
    
    // Take ownership from BIOS
    ehci_take_ownership(ehci, pci_dev);
    
    // Reset controller
    if (ehci_reset(ehci) != 0) {
        tty_putstr("EHCI: Reset failed\n");
        kfree(ehci);
        return -1;
    }
    
    // Allocate periodic frame list (4KB aligned)
    ehci->periodic_list = (uint32_t*)kmalloc_aligned(4096, 4096);
    if (!ehci->periodic_list) {
        tty_putstr("EHCI: Failed to allocate periodic list\n");
        kfree(ehci);
        return -1;
    }
    
    // Initialize periodic list to terminate
    for (int i = 0; i < 1024; i++) {
        ehci->periodic_list[i] = EHCI_QH_TERMINATE;
    }
    
    // Allocate qTD pool
    ehci->qtd_pool_size = 128;
    ehci->qtd_pool = (ehci_qtd_t*)kmalloc_aligned(ehci->qtd_pool_size * sizeof(ehci_qtd_t), 32);
    ehci->qtd_pool_bitmap = (uint8_t*)kmalloc((ehci->qtd_pool_size + 7) / 8);
    if (!ehci->qtd_pool || !ehci->qtd_pool_bitmap) {
        tty_putstr("EHCI: Failed to allocate qTD pool\n");
        kfree(ehci);
        return -1;
    }
    for (int i = 0; i < (ehci->qtd_pool_size + 7) / 8; i++) {
        ehci->qtd_pool_bitmap[i] = 0;
    }
    
    // Allocate QH pool
    ehci->qh_pool_size = 32;
    ehci->qh_pool = (ehci_qh_t*)kmalloc_aligned(ehci->qh_pool_size * sizeof(ehci_qh_t), 32);
    ehci->qh_pool_bitmap = (uint8_t*)kmalloc((ehci->qh_pool_size + 7) / 8);
    if (!ehci->qh_pool || !ehci->qh_pool_bitmap) {
        tty_putstr("EHCI: Failed to allocate QH pool\n");
        kfree(ehci);
        return -1;
    }
    for (int i = 0; i < (ehci->qh_pool_size + 7) / 8; i++) {
        ehci->qh_pool_bitmap[i] = 0;
    }
    
    // Allocate port tracking
    ehci->port_devices = (usb_device_t**)kmalloc(ehci->num_ports * sizeof(usb_device_t*));
    ehci->port_connected = (int*)kmalloc(ehci->num_ports * sizeof(int));
    if (!ehci->port_devices || !ehci->port_connected) {
        kfree(ehci);
        return -1;
    }
    for (int i = 0; i < ehci->num_ports; i++) {
        ehci->port_devices[i] = 0;
        ehci->port_connected[i] = 0;
    }
    
    // Create async queue head (for control/bulk)
    ehci->async_qh = ehci_alloc_qh(ehci);
    if (!ehci->async_qh) {
        tty_putstr("EHCI: Failed to allocate async QH\n");
        kfree(ehci);
        return -1;
    }
    
    // Setup async QH as empty circular list
    ehci->async_qh->horizontal_link = ehci_phys(ehci->async_qh) | EHCI_QH_TYPE_QH;
    ehci->async_qh->endpoint_char = EHCI_QH_EP_HEAD | EHCI_QH_EP_EPS_HIGH | (64 << 16);
    ehci->async_qh->endpoint_caps = (1 << 30); // High-bandwidth pipe multiplier
    ehci->async_qh->next_qtd = EHCI_QH_TERMINATE;
    ehci->async_qh->alt_next_qtd = EHCI_QH_TERMINATE;
    ehci->async_qh->token = EHCI_QTD_STATUS_HALTED;
    
    // Set EHCI registers
    ehci_op_write32(ehci, EHCI_PERIODICBASE, ehci_phys(ehci->periodic_list));
    ehci_op_write32(ehci, EHCI_ASYNCLISTADDR, ehci_phys(ehci->async_qh));
    ehci_op_write32(ehci, EHCI_CTRLDSSEGMENT, 0);
    
    // Clear status
    ehci_op_write32(ehci, EHCI_USBSTS, 0x3F);
    
    // Enable interrupts
    ehci_op_write32(ehci, EHCI_USBINTR, EHCI_INTR_USBIE | EHCI_INTR_USBEIE | 
                                         EHCI_INTR_PCIE | EHCI_INTR_HSEE);
    
    // Set configure flag (route all ports to EHCI)
    ehci_op_write32(ehci, EHCI_CONFIGFLAG, 1);
    ehci_delay(10);
    
    // Start controller
    uint32_t cmd = EHCI_CMD_RS | EHCI_CMD_ASE | EHCI_CMD_ITC_1;
    ehci_op_write32(ehci, EHCI_USBCMD, cmd);
    
    // Wait for controller to start
    for (int i = 0; i < 100; i++) {
        if (!(ehci_op_read32(ehci, EHCI_USBSTS) & EHCI_STS_HCH)) {
            break;
        }
        ehci_delay(1);
    }
    
    // Store EHCI data in controller
    ctrl->driver_data = ehci;
    ctrl->base = (void*)ehci->mmio_base;
    ctrl->type = USB_CONTROLLER_EHCI;
    ctrl->ops = &ehci_ops;
    ctrl->irq = pci_dev->irq;
    
    tty_putstr("EHCI: Controller initialized\n");
    
    return 0;
}

static int ehci_init_controller(usb_controller_t* ctrl) {
    return 0; // Already initialized
}

static void ehci_shutdown(usb_controller_t* ctrl) {
    ehci_controller_t* ehci = (ehci_controller_t*)ctrl->driver_data;
    if (!ehci) return;
    
    // Stop controller
    ehci_op_write32(ehci, EHCI_USBCMD, 0);
    ehci_delay(10);
    
    // Free resources
    if (ehci->periodic_list) kfree(ehci->periodic_list);
    if (ehci->qtd_pool) kfree(ehci->qtd_pool);
    if (ehci->qtd_pool_bitmap) kfree(ehci->qtd_pool_bitmap);
    if (ehci->qh_pool) kfree(ehci->qh_pool);
    if (ehci->qh_pool_bitmap) kfree(ehci->qh_pool_bitmap);
    if (ehci->port_devices) kfree(ehci->port_devices);
    if (ehci->port_connected) kfree(ehci->port_connected);
    
    kfree(ehci);
    ctrl->driver_data = 0;
}

// Reset and enable a port
static int ehci_reset_port(usb_controller_t* ctrl, int port) {
    ehci_controller_t* ehci = (ehci_controller_t*)ctrl->driver_data;
    if (!ehci || port < 0 || port >= ehci->num_ports) return -1;
    
    uint32_t status = ehci_port_read(ehci, port);
    
    // Check if device connected
    if (!(status & EHCI_PORT_CCS)) {
        return -1;
    }
    
    // Check line status - if low-speed device, release to companion
    uint32_t line_status = (status >> 10) & 0x03;
    if (line_status == 0x01) {
        // K-state indicates low-speed device
        tty_putstr("EHCI: Low-speed device on port ");
        tty_putdec(port);
        tty_putstr(", releasing to companion\n");
        ehci_port_write(ehci, port, status | EHCI_PORT_OWNER);
        return -1;
    }
    
    // Reset the port
    status &= ~EHCI_PORT_PED; // Disable first
    status |= EHCI_PORT_RESET;
    ehci_port_write(ehci, port, status);
    
    ehci_delay(50); // USB spec requires at least 10ms
    
    // Clear reset
    status = ehci_port_read(ehci, port);
    status &= ~EHCI_PORT_RESET;
    ehci_port_write(ehci, port, status);
    
    // Wait for reset recovery
    ehci_delay(10);
    
    // Check if port is enabled (indicates high-speed device)
    for (int i = 0; i < 10; i++) {
        status = ehci_port_read(ehci, port);
        if (status & EHCI_PORT_PED) {
            ehci->port_connected[port] = 1;
            tty_putstr("EHCI: Port ");
            tty_putdec(port);
            tty_putstr(" enabled (high-speed)\n");
            return 0;
        }
        
        // If not enabled, might be full-speed - release to companion
        if (!(status & EHCI_PORT_CCS)) {
            return -1; // Device disconnected
        }
        
        ehci_delay(10);
    }
    
    // Not enabled - release to companion controller
    tty_putstr("EHCI: Full-speed device on port ");
    tty_putdec(port);
    tty_putstr(", releasing to companion\n");
    ehci_port_write(ehci, port, status | EHCI_PORT_OWNER);
    
    return -1;
}

// Build endpoint characteristics for QH
static uint32_t ehci_make_ep_char(usb_device_t* dev, uint8_t endpoint, uint16_t max_packet) {
    uint32_t ep_char = 0;
    
    ep_char |= (dev->address & 0x7F);                    // Device address
    ep_char |= ((endpoint & 0x0F) << 8);                 // Endpoint number
    ep_char |= EHCI_QH_EP_EPS_HIGH;                      // High-speed
    ep_char |= ((max_packet & 0x7FF) << 16);             // Max packet size
    ep_char |= EHCI_QH_EP_DTC;                           // Use qTD toggle
    
    if (endpoint == 0) {
        ep_char |= (1 << 27); // Control endpoint toggle from qTD
    }
    
    return ep_char;
}

// Build a qTD
static void ehci_build_qtd(ehci_qtd_t* qtd, uint32_t pid, void* buffer, 
                           uint32_t length, uint8_t toggle, int ioc) {
    qtd->token = EHCI_QTD_STATUS_ACTIVE | (3 << 10); // Active, 3 error retries
    qtd->token |= pid;
    qtd->token |= (length << 16);
    if (toggle) qtd->token |= EHCI_QTD_TOGGLE;
    if (ioc) qtd->token |= EHCI_QTD_IOC;
    
    // Setup buffer pointers
    uintptr_t addr = (uintptr_t)buffer;
    qtd->buffer[0] = (uint32_t)addr;
    
    // Handle crossing page boundaries
    for (int i = 1; i < 5 && length > 0; i++) {
        addr = (addr + 0x1000) & ~0xFFF; // Next page
        qtd->buffer[i] = (uint32_t)addr;
    }
}

// Wait for QH transfer to complete
static int ehci_wait_qh(ehci_controller_t* ehci, ehci_qh_t* qh) {
    for (int i = 0; i < 5000; i++) {
        uint32_t token = qh->token;
        
        if (!(token & EHCI_QTD_STATUS_ACTIVE)) {
            if (token & EHCI_QTD_STATUS_HALTED) {
                if (token & EHCI_QTD_STATUS_BABBLE) return USB_TRANSFER_ERROR;
                if (token & EHCI_QTD_STATUS_BUFERR) return USB_TRANSFER_ERROR;
                if (token & EHCI_QTD_STATUS_XACTERR) return USB_TRANSFER_ERROR;
                return USB_TRANSFER_STALL;
            }
            return USB_TRANSFER_SUCCESS;
        }
        
        ehci_delay(1);
    }
    
    return USB_TRANSFER_TIMEOUT;
}

// Control transfer
static int ehci_control_transfer(usb_device_t* dev, usb_setup_packet_t* setup, 
                                  void* data, uint16_t length) {
    usb_controller_t* ctrl = dev->controller;
    ehci_controller_t* ehci = (ehci_controller_t*)ctrl->driver_data;
    
    // Allocate QH and qTDs
    ehci_qh_t* qh = ehci_alloc_qh(ehci);
    ehci_qtd_t* qtd_setup = ehci_alloc_qtd(ehci);
    ehci_qtd_t* qtd_data = 0;
    ehci_qtd_t* qtd_status = ehci_alloc_qtd(ehci);
    
    if (!qh || !qtd_setup || !qtd_status) {
        if (qh) ehci_free_qh(ehci, qh);
        if (qtd_setup) ehci_free_qtd(ehci, qtd_setup);
        if (qtd_status) ehci_free_qtd(ehci, qtd_status);
        return USB_TRANSFER_ERROR;
    }
    
    // Setup QH
    qh->horizontal_link = ehci_phys(ehci->async_qh) | EHCI_QH_TYPE_QH;
    qh->endpoint_char = ehci_make_ep_char(dev, 0, dev->max_packet_size);
    qh->endpoint_caps = (1 << 30); // Mult = 1
    qh->current_qtd = 0;
    
    // Build setup qTD
    ehci_build_qtd(qtd_setup, EHCI_QTD_PID_SETUP, setup, 8, 0, 0);
    
    ehci_qtd_t* prev_qtd = qtd_setup;
    uint8_t toggle = 1;
    
    // Build data qTD if needed
    if (length > 0 && data) {
        qtd_data = ehci_alloc_qtd(ehci);
        if (!qtd_data) {
            ehci_free_qh(ehci, qh);
            ehci_free_qtd(ehci, qtd_setup);
            ehci_free_qtd(ehci, qtd_status);
            return USB_TRANSFER_ERROR;
        }
        
        uint32_t pid = (setup->bmRequestType & USB_REQ_DIR_IN) ? 
                      EHCI_QTD_PID_IN : EHCI_QTD_PID_OUT;
        ehci_build_qtd(qtd_data, pid, data, length, toggle, 0);
        
        prev_qtd->next_qtd = ehci_phys(qtd_data);
        prev_qtd = qtd_data;
        toggle ^= 1;
    }
    
    // Build status qTD
    uint8_t status_pid = (setup->bmRequestType & USB_REQ_DIR_IN) ? 
                         EHCI_QTD_PID_OUT : EHCI_QTD_PID_IN;
    ehci_build_qtd(qtd_status, status_pid, 0, 0, 1, 1);
    
    prev_qtd->next_qtd = ehci_phys(qtd_status);
    qtd_status->next_qtd = EHCI_QH_TERMINATE;
    
    // Link QH to async schedule
    qh->next_qtd = ehci_phys(qtd_setup);
    qh->alt_next_qtd = EHCI_QH_TERMINATE;
    qh->token = 0;
    
    // Insert QH into async list
    qh->horizontal_link = ehci->async_qh->horizontal_link;
    ehci->async_qh->horizontal_link = ehci_phys(qh) | EHCI_QH_TYPE_QH;
    
    // Ring doorbell
    ehci_op_write32(ehci, EHCI_USBCMD, 
                    ehci_op_read32(ehci, EHCI_USBCMD) | EHCI_CMD_IAAD);
    
    // Wait for completion
    int result = ehci_wait_qh(ehci, qh);
    
    // Remove from async list
    ehci->async_qh->horizontal_link = qh->horizontal_link;
    
    // Free resources
    ehci_free_qh(ehci, qh);
    ehci_free_qtd(ehci, qtd_setup);
    if (qtd_data) ehci_free_qtd(ehci, qtd_data);
    ehci_free_qtd(ehci, qtd_status);
    
    return result;
}

// Bulk transfer
static int ehci_bulk_transfer(usb_device_t* dev, uint8_t endpoint, 
                               void* data, uint32_t length) {
    usb_controller_t* ctrl = dev->controller;
    ehci_controller_t* ehci = (ehci_controller_t*)ctrl->driver_data;
    
    int direction = (endpoint & USB_ENDPOINT_DIR_IN) ? 1 : 0;
    uint8_t ep_num = endpoint & 0x0F;
    uint8_t pid = direction ? EHCI_QTD_PID_IN : EHCI_QTD_PID_OUT;
    
    // Find endpoint info
    uint16_t max_packet = dev->max_packet_size;
    for (int i = 0; i < dev->num_endpoints; i++) {
        if (dev->endpoints[i].address == endpoint) {
            max_packet = dev->endpoints[i].max_packet_size;
            break;
        }
    }
    
    ehci_qh_t* qh = ehci_alloc_qh(ehci);
    if (!qh) return USB_TRANSFER_ERROR;
    
    // Setup QH
    qh->horizontal_link = ehci_phys(ehci->async_qh) | EHCI_QH_TYPE_QH;
    qh->endpoint_char = ehci_make_ep_char(dev, ep_num, max_packet);
    qh->endpoint_caps = (1 << 30);
    
    // Build qTDs for data
    uint8_t* data_ptr = (uint8_t*)data;
    uint32_t remaining = length;
    ehci_qtd_t* first_qtd = 0;
    ehci_qtd_t* prev_qtd = 0;
    uint8_t toggle = dev->endpoints[ep_num].toggle;
    
    while (remaining > 0) {
        uint32_t chunk = (remaining > 16384) ? 16384 : remaining;
        
        ehci_qtd_t* qtd = ehci_alloc_qtd(ehci);
        if (!qtd) {
            // Cleanup
            ehci_free_qh(ehci, qh);
            return USB_TRANSFER_ERROR;
        }
        
        int ioc = (remaining <= chunk);
        ehci_build_qtd(qtd, pid, data_ptr, chunk, toggle, ioc);
        
        if (!first_qtd) {
            first_qtd = qtd;
        }
        if (prev_qtd) {
            prev_qtd->next_qtd = ehci_phys(qtd);
        }
        
        prev_qtd = qtd;
        data_ptr += chunk;
        remaining -= chunk;
        toggle ^= 1;
    }
    
    if (prev_qtd) {
        prev_qtd->next_qtd = EHCI_QH_TERMINATE;
    }
    
    // Link to QH
    qh->next_qtd = first_qtd ? ehci_phys(first_qtd) : EHCI_QH_TERMINATE;
    qh->alt_next_qtd = EHCI_QH_TERMINATE;
    qh->token = 0;
    
    // Insert into async list
    qh->horizontal_link = ehci->async_qh->horizontal_link;
    ehci->async_qh->horizontal_link = ehci_phys(qh) | EHCI_QH_TYPE_QH;
    
    // Wait for completion
    int result = ehci_wait_qh(ehci, qh);
    
    // Remove from list
    ehci->async_qh->horizontal_link = qh->horizontal_link;
    
    // Update toggle
    dev->endpoints[ep_num].toggle = toggle;
    
    // Cleanup
    ehci_free_qh(ehci, qh);
    // Note: Should free all qTDs properly
    
    return result;
}

// Interrupt transfer (polling mode)
static int ehci_interrupt_transfer(usb_device_t* dev, uint8_t endpoint, 
                                    void* data, uint32_t length) {
    // For simplicity, use same mechanism as bulk
    return ehci_bulk_transfer(dev, endpoint, data, length);
}

// Poll for port changes
static void ehci_poll(usb_controller_t* ctrl) {
    ehci_controller_t* ehci = (ehci_controller_t*)ctrl->driver_data;
    if (!ehci) return;
    
    // Check status register
    uint32_t status = ehci_op_read32(ehci, EHCI_USBSTS);
    
    // Clear status bits
    if (status & 0x3F) {
        ehci_op_write32(ehci, EHCI_USBSTS, status & 0x3F);
    }
    
    // Check for port changes
    if (status & EHCI_STS_PCD) {
        for (int port = 0; port < ehci->num_ports; port++) {
            uint32_t port_status = ehci_port_read(ehci, port);
            
            // Check for connect status change
            if (port_status & EHCI_PORT_CSC) {
                // Clear the change bit
                ehci_port_write(ehci, port, port_status | EHCI_PORT_CSC);
                
                if (port_status & EHCI_PORT_CCS) {
                    tty_putstr("EHCI: Device connected on port ");
                    tty_putdec(port);
                    tty_putstr("\n");
                    
                    ehci_reset_port(ctrl, port);
                } else {
                    tty_putstr("EHCI: Device disconnected from port ");
                    tty_putdec(port);
                    tty_putstr("\n");
                    
                    ehci->port_connected[port] = 0;
                    ehci->port_devices[port] = 0;
                }
            }
        }
    }
}

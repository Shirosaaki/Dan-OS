//
// XHCI (USB 3.0) Controller Driver Implementation
//

#include <kernel/drivers/xhci.h>
#include <kernel/drivers/pci.h>
#include <kernel/drivers/usb.h>
#include <kernel/sys/tty.h>
#include <kernel/sys/kmalloc.h>

// Forward declarations
static int xhci_init_controller(usb_controller_t* ctrl);
static void xhci_shutdown(usb_controller_t* ctrl);
static int xhci_reset_port(usb_controller_t* ctrl, int port);
static int xhci_control_transfer(usb_device_t* dev, usb_setup_packet_t* setup, void* data, uint16_t length);
static int xhci_bulk_transfer(usb_device_t* dev, uint8_t endpoint, void* data, uint32_t length);
static int xhci_interrupt_transfer(usb_device_t* dev, uint8_t endpoint, void* data, uint32_t length);
static void xhci_poll(usb_controller_t* ctrl);

// XHCI operations
usb_controller_ops_t xhci_ops = {
    .init = xhci_init_controller,
    .shutdown = xhci_shutdown,
    .reset_port = xhci_reset_port,
    .control_transfer = xhci_control_transfer,
    .bulk_transfer = xhci_bulk_transfer,
    .interrupt_transfer = xhci_interrupt_transfer,
    .poll = xhci_poll
};

// Ring sizes
#define CMD_RING_SIZE   256
#define EVENT_RING_SIZE 256
#define TRANSFER_RING_SIZE 256

// Delay helper
static void xhci_delay(int ms) {
    for (volatile int i = 0; i < ms * 10000; i++) {
        __asm__ volatile("nop");
    }
}

// Memory barrier
static inline void xhci_mb(void) {
    __asm__ volatile("mfence" ::: "memory");
}

// Read capability register
static uint32_t xhci_cap_read32(xhci_controller_t* xhci, uint32_t offset) {
    return *(volatile uint32_t*)(xhci->mmio_base + offset);
}

static uint8_t xhci_cap_read8(xhci_controller_t* xhci, uint32_t offset) {
    return *(volatile uint8_t*)(xhci->mmio_base + offset);
}

// Read/write operational register
static uint32_t xhci_op_read32(xhci_controller_t* xhci, uint32_t offset) {
    return *(volatile uint32_t*)(xhci->op_base + offset);
}

static void xhci_op_write32(xhci_controller_t* xhci, uint32_t offset, uint32_t value) {
    *(volatile uint32_t*)(xhci->op_base + offset) = value;
}

static uint64_t xhci_op_read64(xhci_controller_t* xhci, uint32_t offset) {
    return *(volatile uint64_t*)(xhci->op_base + offset);
}

static void xhci_op_write64(xhci_controller_t* xhci, uint32_t offset, uint64_t value) {
    *(volatile uint64_t*)(xhci->op_base + offset) = value;
}

// Port register access
static uint32_t xhci_port_read(xhci_controller_t* xhci, int port) {
    return *(volatile uint32_t*)(xhci->port_base + port * 0x10);
}

static void xhci_port_write(xhci_controller_t* xhci, int port, uint32_t value) {
    *(volatile uint32_t*)(xhci->port_base + port * 0x10) = value;
}

// Runtime register access
static void xhci_rt_write32(xhci_controller_t* xhci, uint32_t offset, uint32_t value) {
    *(volatile uint32_t*)(xhci->rt_base + offset) = value;
}

static void xhci_rt_write64(xhci_controller_t* xhci, uint32_t offset, uint64_t value) {
    *(volatile uint64_t*)(xhci->rt_base + offset) = value;
}

// Ring doorbell
static void xhci_doorbell(xhci_controller_t* xhci, uint32_t slot, uint32_t target) {
    xhci->db_base[slot] = target;
}

// Get physical address
static uint64_t xhci_phys(void* ptr) {
    return (uint64_t)(uintptr_t)ptr;
}

// Wait for controller not ready to clear
static int xhci_wait_ready(xhci_controller_t* xhci) {
    for (int i = 0; i < 1000; i++) {
        if (!(xhci_op_read32(xhci, XHCI_USBSTS) & XHCI_STS_CNR)) {
            return 0;
        }
        xhci_delay(1);
    }
    return -1;
}

// Reset XHCI controller
static int xhci_reset(xhci_controller_t* xhci) {
    // Stop controller
    uint32_t cmd = xhci_op_read32(xhci, XHCI_USBCMD);
    cmd &= ~XHCI_CMD_RS;
    xhci_op_write32(xhci, XHCI_USBCMD, cmd);
    
    // Wait for halt
    for (int i = 0; i < 100; i++) {
        if (xhci_op_read32(xhci, XHCI_USBSTS) & XHCI_STS_HCH) {
            break;
        }
        xhci_delay(1);
    }
    
    // Reset controller
    cmd = xhci_op_read32(xhci, XHCI_USBCMD);
    cmd |= XHCI_CMD_HCRST;
    xhci_op_write32(xhci, XHCI_USBCMD, cmd);
    
    // Wait for reset to complete
    for (int i = 0; i < 1000; i++) {
        if (!(xhci_op_read32(xhci, XHCI_USBCMD) & XHCI_CMD_HCRST)) {
            return xhci_wait_ready(xhci);
        }
        xhci_delay(1);
    }
    
    return -1;
}

// Take ownership from BIOS
static void xhci_take_ownership(xhci_controller_t* xhci, pci_device_t* pci_dev) {
    uint32_t hccparams1 = xhci_cap_read32(xhci, XHCI_HCCPARAMS1);
    uint16_t ext_cap_offset = (hccparams1 >> 16) & 0xFFFF;
    ext_cap_offset *= 4;
    
    while (ext_cap_offset) {
        uint32_t cap = *(volatile uint32_t*)(xhci->mmio_base + ext_cap_offset);
        uint8_t cap_id = cap & 0xFF;
        
        if (cap_id == 1) {
            // USB Legacy Support capability
            if (cap & (1 << 16)) { // BIOS owns controller
                // Request ownership
                *(volatile uint32_t*)(xhci->mmio_base + ext_cap_offset) = cap | (1 << 24);
                
                // Wait for BIOS to release
                for (int i = 0; i < 100; i++) {
                    cap = *(volatile uint32_t*)(xhci->mmio_base + ext_cap_offset);
                    if (!(cap & (1 << 16)) && (cap & (1 << 24))) {
                        break;
                    }
                    xhci_delay(10);
                }
            }
            
            // Disable SMI
            *(volatile uint32_t*)(xhci->mmio_base + ext_cap_offset + 4) = 0;
            break;
        }
        
        uint8_t next = (cap >> 8) & 0xFF;
        if (next == 0) break;
        ext_cap_offset += next * 4;
    }
}

// Enqueue a TRB to command ring
static void xhci_cmd_enqueue(xhci_controller_t* xhci, xhci_trb_t* trb) {
    xhci_trb_t* dest = &xhci->cmd_ring[xhci->cmd_ring_index];
    
    dest->parameter = trb->parameter;
    dest->status = trb->status;
    dest->control = trb->control | (xhci->cmd_ring_cycle ? XHCI_TRB_CYCLE : 0);
    
    xhci_mb();
    
    xhci->cmd_ring_index++;
    if (xhci->cmd_ring_index >= CMD_RING_SIZE - 1) {
        // Add link TRB
        xhci_trb_t* link = &xhci->cmd_ring[xhci->cmd_ring_index];
        link->parameter = xhci_phys(xhci->cmd_ring);
        link->status = 0;
        link->control = XHCI_TRB_TYPE(XHCI_TRB_LINK) | 
                       (xhci->cmd_ring_cycle ? XHCI_TRB_CYCLE : 0) |
                       (1 << 1); // Toggle cycle
        
        xhci->cmd_ring_index = 0;
        xhci->cmd_ring_cycle ^= 1;
    }
}

// Wait for command completion
static int xhci_wait_cmd(xhci_controller_t* xhci, uint32_t* slot_id) {
    for (int i = 0; i < 5000; i++) {
        xhci_trb_t* event = &xhci->event_ring[xhci->event_ring_index];
        
        uint8_t cycle = event->control & XHCI_TRB_CYCLE;
        if (cycle != xhci->event_ring_cycle) {
            xhci_delay(1);
            continue;
        }
        
        uint8_t type = (event->control >> 10) & 0x3F;
        
        if (type == XHCI_TRB_CMD_COMPLETION) {
            uint8_t cc = (event->status >> 24) & 0xFF;
            if (slot_id) {
                *slot_id = (event->control >> 24) & 0xFF;
            }
            
            // Advance event ring
            xhci->event_ring_index++;
            if (xhci->event_ring_index >= EVENT_RING_SIZE) {
                xhci->event_ring_index = 0;
                xhci->event_ring_cycle ^= 1;
            }
            
            // Update ERDP
            uint64_t erdp = xhci_phys(&xhci->event_ring[xhci->event_ring_index]);
            erdp |= (1 << 3); // EHB - Event Handler Busy
            xhci_rt_write64(xhci, 0x20 + 0x38, erdp);
            
            if (cc == XHCI_CC_SUCCESS || cc == XHCI_CC_SHORT_PACKET) {
                return USB_TRANSFER_SUCCESS;
            }
            if (cc == XHCI_CC_STALL_ERROR) {
                return USB_TRANSFER_STALL;
            }
            return USB_TRANSFER_ERROR;
        }
        
        xhci_delay(1);
    }
    
    return USB_TRANSFER_TIMEOUT;
}

// Enable a slot
static int xhci_enable_slot(xhci_controller_t* xhci, uint32_t* slot_id) {
    xhci_trb_t trb = {0};
    trb.control = XHCI_TRB_TYPE(XHCI_TRB_ENABLE_SLOT);
    
    xhci_cmd_enqueue(xhci, &trb);
    xhci_doorbell(xhci, 0, 0);
    
    return xhci_wait_cmd(xhci, slot_id);
}

// Address a device
static int xhci_address_device(xhci_controller_t* xhci, uint32_t slot_id, 
                               xhci_input_ctx_t* input_ctx, int bsr) {
    xhci_trb_t trb = {0};
    trb.parameter = xhci_phys(input_ctx);
    trb.control = XHCI_TRB_TYPE(XHCI_TRB_ADDRESS_DEV) | (slot_id << 24);
    if (bsr) trb.control |= (1 << 9); // Block Set Address Request
    
    xhci_cmd_enqueue(xhci, &trb);
    xhci_doorbell(xhci, 0, 0);
    
    return xhci_wait_cmd(xhci, 0);
}

// Configure endpoints
static int xhci_configure_endpoint(xhci_controller_t* xhci, uint32_t slot_id,
                                   xhci_input_ctx_t* input_ctx) {
    xhci_trb_t trb = {0};
    trb.parameter = xhci_phys(input_ctx);
    trb.control = XHCI_TRB_TYPE(XHCI_TRB_CONFIG_EP) | (slot_id << 24);
    
    xhci_cmd_enqueue(xhci, &trb);
    xhci_doorbell(xhci, 0, 0);
    
    return xhci_wait_cmd(xhci, 0);
}

// Initialize XHCI controller
int xhci_init(usb_controller_t* ctrl, pci_device_t* pci_dev) {
    xhci_controller_t* xhci = (xhci_controller_t*)kmalloc(sizeof(xhci_controller_t));
    if (!xhci) return -1;
    
    // Clear structure
    for (int i = 0; i < (int)sizeof(xhci_controller_t); i++) {
        ((uint8_t*)xhci)[i] = 0;
    }
    
    xhci->pci_dev = pci_dev;
    
    // Get MMIO base from BAR0
    uint64_t bar0 = pci_dev->bar[0];
    if (bar0 & 0x04) {
        // 64-bit BAR
        bar0 |= ((uint64_t)pci_dev->bar[1]) << 32;
    }
    bar0 &= ~0xFULL;
    
    xhci->mmio_base = (volatile uint8_t*)(uintptr_t)bar0;
    
    tty_putstr("XHCI: MMIO base = 0x");
    tty_puthex((uint32_t)(uintptr_t)xhci->mmio_base);
    tty_putstr("\n");
    
    // Enable memory space and bus mastering
    pci_enable_memory_space(pci_dev);
    pci_enable_bus_mastering(pci_dev);
    
    // Read capability registers
    xhci->cap_length = xhci_cap_read8(xhci, XHCI_CAPLENGTH);
    xhci->op_base = xhci->mmio_base + xhci->cap_length;
    
    uint32_t hcsparams1 = xhci_cap_read32(xhci, XHCI_HCSPARAMS1);
    xhci->num_ports = (hcsparams1 >> 24) & 0xFF;
    xhci->num_slots = hcsparams1 & 0xFF;
    
    uint32_t hccparams1 = xhci_cap_read32(xhci, XHCI_HCCPARAMS1);
    xhci->context_size = (hccparams1 & (1 << 2)) ? 64 : 32;
    
    uint32_t dboff = xhci_cap_read32(xhci, XHCI_DBOFF);
    xhci->db_base = (volatile uint32_t*)(xhci->mmio_base + (dboff & ~0x3));
    
    uint32_t rtsoff = xhci_cap_read32(xhci, XHCI_RTSOFF);
    xhci->rt_base = xhci->mmio_base + (rtsoff & ~0x1F);
    
    xhci->port_base = xhci->op_base + 0x400;
    
    tty_putstr("XHCI: ");
    tty_putdec(xhci->num_ports);
    tty_putstr(" ports, ");
    tty_putdec(xhci->num_slots);
    tty_putstr(" slots\n");
    
    // Take ownership from BIOS
    xhci_take_ownership(xhci, pci_dev);
    
    // Reset controller
    if (xhci_reset(xhci) != 0) {
        tty_putstr("XHCI: Reset failed\n");
        kfree(xhci);
        return -1;
    }
    
    // Set max slots
    xhci_op_write32(xhci, XHCI_CONFIG, xhci->num_slots);
    
    // Allocate DCBAA (Device Context Base Address Array)
    xhci->dcbaa = (uint64_t*)kmalloc_aligned((xhci->num_slots + 1) * sizeof(uint64_t), 64);
    if (!xhci->dcbaa) {
        kfree(xhci);
        return -1;
    }
    for (int i = 0; i <= xhci->num_slots; i++) {
        xhci->dcbaa[i] = 0;
    }
    
    // Check for scratchpad buffers
    uint32_t hcsparams2 = xhci_cap_read32(xhci, XHCI_HCSPARAMS2);
    xhci->num_scratchpad = ((hcsparams2 >> 27) & 0x1F) | (((hcsparams2 >> 21) & 0x1F) << 5);
    
    if (xhci->num_scratchpad > 0) {
        uint32_t page_size = xhci_op_read32(xhci, XHCI_PAGESIZE) << 12;
        
        xhci->scratchpad_array = (uint64_t*)kmalloc_aligned(xhci->num_scratchpad * sizeof(uint64_t), 64);
        xhci->scratchpad_buffers = (void**)kmalloc(xhci->num_scratchpad * sizeof(void*));
        
        for (int i = 0; i < xhci->num_scratchpad; i++) {
            xhci->scratchpad_buffers[i] = kmalloc_aligned(page_size, page_size);
            xhci->scratchpad_array[i] = xhci_phys(xhci->scratchpad_buffers[i]);
        }
        
        xhci->dcbaa[0] = xhci_phys(xhci->scratchpad_array);
    }
    
    // Set DCBAAP
    xhci_op_write64(xhci, XHCI_DCBAAP, xhci_phys(xhci->dcbaa));
    
    // Allocate command ring
    xhci->cmd_ring = (xhci_trb_t*)kmalloc_aligned(CMD_RING_SIZE * sizeof(xhci_trb_t), 64);
    for (int i = 0; i < CMD_RING_SIZE; i++) {
        xhci->cmd_ring[i].parameter = 0;
        xhci->cmd_ring[i].status = 0;
        xhci->cmd_ring[i].control = 0;
    }
    xhci->cmd_ring_index = 0;
    xhci->cmd_ring_cycle = 1;
    
    // Set CRCR
    uint64_t crcr = xhci_phys(xhci->cmd_ring) | 1; // RCS = 1
    xhci_op_write64(xhci, XHCI_CRCR, crcr);
    
    // Allocate event ring
    xhci->event_ring = (xhci_trb_t*)kmalloc_aligned(EVENT_RING_SIZE * sizeof(xhci_trb_t), 64);
    for (int i = 0; i < EVENT_RING_SIZE; i++) {
        xhci->event_ring[i].parameter = 0;
        xhci->event_ring[i].status = 0;
        xhci->event_ring[i].control = 0;
    }
    xhci->event_ring_index = 0;
    xhci->event_ring_cycle = 1;
    
    // Allocate ERST (Event Ring Segment Table)
    xhci->erst = (xhci_erst_entry_t*)kmalloc_aligned(sizeof(xhci_erst_entry_t), 64);
    xhci->erst[0].ring_base = xhci_phys(xhci->event_ring);
    xhci->erst[0].ring_size = EVENT_RING_SIZE;
    xhci->erst[0].reserved = 0;
    
    // Setup interrupter 0
    xhci_rt_write32(xhci, 0x20 + 0x00, 0); // IMAN
    xhci_rt_write32(xhci, 0x20 + 0x04, 4000); // IMOD (1ms)
    xhci_rt_write32(xhci, 0x20 + 0x08, 1); // ERSTSZ
    xhci_rt_write64(xhci, 0x20 + 0x10, xhci_phys(xhci->erst)); // ERSTBA
    xhci_rt_write64(xhci, 0x20 + 0x18, xhci_phys(xhci->event_ring) | (1 << 3)); // ERDP
    
    // Allocate device context array
    xhci->device_contexts = (xhci_device_ctx_t**)kmalloc(xhci->num_slots * sizeof(void*));
    for (int i = 0; i < xhci->num_slots; i++) {
        xhci->device_contexts[i] = 0;
    }
    
    // Allocate transfer ring arrays
    int max_rings = xhci->num_slots * 31;
    xhci->transfer_rings = (xhci_trb_t**)kmalloc(max_rings * sizeof(void*));
    xhci->transfer_ring_index = (uint32_t*)kmalloc(max_rings * sizeof(uint32_t));
    xhci->transfer_ring_cycle = (uint8_t*)kmalloc(max_rings * sizeof(uint8_t));
    for (int i = 0; i < max_rings; i++) {
        xhci->transfer_rings[i] = 0;
        xhci->transfer_ring_index[i] = 0;
        xhci->transfer_ring_cycle[i] = 1;
    }
    
    // Allocate port tracking
    xhci->port_connected = (int*)kmalloc(xhci->num_ports * sizeof(int));
    xhci->port_slot = (int*)kmalloc(xhci->num_ports * sizeof(int));
    for (int i = 0; i < xhci->num_ports; i++) {
        xhci->port_connected[i] = 0;
        xhci->port_slot[i] = 0;
    }
    
    // Enable interrupts and start controller
    xhci_rt_write32(xhci, 0x20 + 0x00, 0x2); // IMAN - enable interrupt
    
    uint32_t cmd = XHCI_CMD_RS | XHCI_CMD_INTE;
    xhci_op_write32(xhci, XHCI_USBCMD, cmd);
    
    // Wait for controller to start
    for (int i = 0; i < 100; i++) {
        if (!(xhci_op_read32(xhci, XHCI_USBSTS) & XHCI_STS_HCH)) {
            break;
        }
        xhci_delay(1);
    }
    
    // Store in controller
    ctrl->driver_data = xhci;
    ctrl->base = (void*)xhci->mmio_base;
    ctrl->type = USB_CONTROLLER_XHCI;
    ctrl->ops = &xhci_ops;
    ctrl->irq = pci_dev->irq;
    
    tty_putstr("XHCI: Controller initialized\n");
    
    return 0;
}

static int xhci_init_controller(usb_controller_t* ctrl) {
    return 0;
}

static void xhci_shutdown(usb_controller_t* ctrl) {
    xhci_controller_t* xhci = (xhci_controller_t*)ctrl->driver_data;
    if (!xhci) return;
    
    // Stop controller
    xhci_op_write32(xhci, XHCI_USBCMD, 0);
    xhci_delay(10);
    
    // Free resources
    if (xhci->dcbaa) kfree(xhci->dcbaa);
    if (xhci->cmd_ring) kfree(xhci->cmd_ring);
    if (xhci->event_ring) kfree(xhci->event_ring);
    if (xhci->erst) kfree(xhci->erst);
    
    if (xhci->scratchpad_array) {
        for (int i = 0; i < xhci->num_scratchpad; i++) {
            if (xhci->scratchpad_buffers[i]) {
                kfree(xhci->scratchpad_buffers[i]);
            }
        }
        kfree(xhci->scratchpad_array);
        kfree(xhci->scratchpad_buffers);
    }
    
    kfree(xhci);
    ctrl->driver_data = 0;
}

// Reset a USB port
static int xhci_reset_port(usb_controller_t* ctrl, int port) {
    xhci_controller_t* xhci = (xhci_controller_t*)ctrl->driver_data;
    if (!xhci || port < 0 || port >= xhci->num_ports) return -1;
    
    uint32_t status = xhci_port_read(xhci, port);
    
    // Check if connected
    if (!(status & XHCI_PORT_CCS)) {
        return -1;
    }
    
    // Issue port reset
    status &= ~(XHCI_PORT_PED | XHCI_PORT_CSC | XHCI_PORT_PEC | 
                XHCI_PORT_WRC | XHCI_PORT_OCC | XHCI_PORT_PRC |
                XHCI_PORT_PLC | XHCI_PORT_CEC);
    status |= XHCI_PORT_PR;
    xhci_port_write(xhci, port, status);
    
    // Wait for reset complete
    for (int i = 0; i < 500; i++) {
        status = xhci_port_read(xhci, port);
        if (status & XHCI_PORT_PRC) {
            // Clear reset change
            xhci_port_write(xhci, port, status | XHCI_PORT_PRC);
            break;
        }
        xhci_delay(1);
    }
    
    // Check if enabled
    status = xhci_port_read(xhci, port);
    if (status & XHCI_PORT_PED) {
        xhci->port_connected[port] = 1;
        
        int speed = (status >> 10) & 0xF;
        const char* speed_str = "unknown";
        switch (speed) {
            case XHCI_SPEED_FULL: speed_str = "full"; break;
            case XHCI_SPEED_LOW: speed_str = "low"; break;
            case XHCI_SPEED_HIGH: speed_str = "high"; break;
            case XHCI_SPEED_SUPER: speed_str = "super"; break;
        }
        
        tty_putstr("XHCI: Port ");
        tty_putdec(port);
        tty_putstr(" enabled (");
        tty_putstr(speed_str);
        tty_putstr(" speed)\n");
        
        return 0;
    }
    
    return -1;
}

// Enqueue TRB to transfer ring
static void xhci_transfer_enqueue(xhci_controller_t* xhci, int slot, int ep,
                                   xhci_trb_t* trb) {
    int ring_idx = (slot - 1) * 31 + ep;
    xhci_trb_t* ring = xhci->transfer_rings[ring_idx];
    uint32_t idx = xhci->transfer_ring_index[ring_idx];
    uint8_t cycle = xhci->transfer_ring_cycle[ring_idx];
    
    xhci_trb_t* dest = &ring[idx];
    dest->parameter = trb->parameter;
    dest->status = trb->status;
    dest->control = trb->control | (cycle ? XHCI_TRB_CYCLE : 0);
    
    xhci_mb();
    
    idx++;
    if (idx >= TRANSFER_RING_SIZE - 1) {
        // Add link TRB
        xhci_trb_t* link = &ring[idx];
        link->parameter = xhci_phys(ring);
        link->status = 0;
        link->control = XHCI_TRB_TYPE(XHCI_TRB_LINK) |
                       (cycle ? XHCI_TRB_CYCLE : 0) |
                       (1 << 1); // Toggle cycle
        
        idx = 0;
        xhci->transfer_ring_cycle[ring_idx] ^= 1;
    }
    
    xhci->transfer_ring_index[ring_idx] = idx;
}

// Wait for transfer completion
static int xhci_wait_transfer(xhci_controller_t* xhci) {
    for (int i = 0; i < 5000; i++) {
        xhci_trb_t* event = &xhci->event_ring[xhci->event_ring_index];
        
        uint8_t cycle = event->control & XHCI_TRB_CYCLE;
        if (cycle != xhci->event_ring_cycle) {
            xhci_delay(1);
            continue;
        }
        
        uint8_t type = (event->control >> 10) & 0x3F;
        
        if (type == XHCI_TRB_TRANSFER_EVENT) {
            uint8_t cc = (event->status >> 24) & 0xFF;
            
            // Advance event ring
            xhci->event_ring_index++;
            if (xhci->event_ring_index >= EVENT_RING_SIZE) {
                xhci->event_ring_index = 0;
                xhci->event_ring_cycle ^= 1;
            }
            
            // Update ERDP
            uint64_t erdp = xhci_phys(&xhci->event_ring[xhci->event_ring_index]);
            erdp |= (1 << 3);
            xhci_rt_write64(xhci, 0x20 + 0x38, erdp);
            
            if (cc == XHCI_CC_SUCCESS || cc == XHCI_CC_SHORT_PACKET) {
                return USB_TRANSFER_SUCCESS;
            }
            if (cc == XHCI_CC_STALL_ERROR) {
                return USB_TRANSFER_STALL;
            }
            return USB_TRANSFER_ERROR;
        }
        
        xhci_delay(1);
    }
    
    return USB_TRANSFER_TIMEOUT;
}

// Control transfer
static int xhci_control_transfer(usb_device_t* dev, usb_setup_packet_t* setup,
                                  void* data, uint16_t length) {
    // For XHCI, we need slot and endpoint context setup
    // This is a simplified implementation
    usb_controller_t* ctrl = dev->controller;
    xhci_controller_t* xhci = (xhci_controller_t*)ctrl->driver_data;
    
    if (!xhci || dev->address == 0) return USB_TRANSFER_ERROR;
    
    int slot = dev->address; // Simplified: address = slot
    int ring_idx = (slot - 1) * 31 + 1; // EP0 = endpoint index 1
    
    if (!xhci->transfer_rings[ring_idx]) return USB_TRANSFER_ERROR;
    
    // Setup stage
    xhci_trb_t trb = {0};
    trb.parameter = *(uint64_t*)setup;
    trb.status = 8; // TRB transfer length
    trb.control = XHCI_TRB_TYPE(XHCI_TRB_SETUP) | XHCI_TRB_IDT;
    if (length > 0) {
        trb.control |= (setup->bmRequestType & USB_REQ_DIR_IN) ? (3 << 16) : (2 << 16);
    } else {
        trb.control |= (0 << 16); // No data stage
    }
    xhci_transfer_enqueue(xhci, slot, 1, &trb);
    
    // Data stage
    if (length > 0 && data) {
        trb.parameter = xhci_phys(data);
        trb.status = length;
        trb.control = XHCI_TRB_TYPE(XHCI_TRB_DATA);
        if (setup->bmRequestType & USB_REQ_DIR_IN) {
            trb.control |= XHCI_TRB_DIR_IN;
        }
        xhci_transfer_enqueue(xhci, slot, 1, &trb);
    }
    
    // Status stage
    trb.parameter = 0;
    trb.status = 0;
    trb.control = XHCI_TRB_TYPE(XHCI_TRB_STATUS) | XHCI_TRB_IOC;
    if (length == 0 || !(setup->bmRequestType & USB_REQ_DIR_IN)) {
        trb.control |= XHCI_TRB_DIR_IN;
    }
    xhci_transfer_enqueue(xhci, slot, 1, &trb);
    
    // Ring doorbell
    xhci_doorbell(xhci, slot, 1);
    
    return xhci_wait_transfer(xhci);
}

// Bulk transfer
static int xhci_bulk_transfer(usb_device_t* dev, uint8_t endpoint,
                               void* data, uint32_t length) {
    usb_controller_t* ctrl = dev->controller;
    xhci_controller_t* xhci = (xhci_controller_t*)ctrl->driver_data;
    
    if (!xhci || dev->address == 0) return USB_TRANSFER_ERROR;
    
    int slot = dev->address;
    int ep_num = endpoint & 0x0F;
    int direction = (endpoint & USB_ENDPOINT_DIR_IN) ? 1 : 0;
    int ep_idx = ep_num * 2 + direction;
    int ring_idx = (slot - 1) * 31 + ep_idx;
    
    if (!xhci->transfer_rings[ring_idx]) return USB_TRANSFER_ERROR;
    
    // Normal TRB
    xhci_trb_t trb = {0};
    trb.parameter = xhci_phys(data);
    trb.status = length;
    trb.control = XHCI_TRB_TYPE(XHCI_TRB_NORMAL) | XHCI_TRB_IOC;
    if (direction) {
        trb.control |= XHCI_TRB_ISP; // Interrupt on short packet
    }
    
    xhci_transfer_enqueue(xhci, slot, ep_idx, &trb);
    xhci_doorbell(xhci, slot, ep_idx);
    
    return xhci_wait_transfer(xhci);
}

// Interrupt transfer
static int xhci_interrupt_transfer(usb_device_t* dev, uint8_t endpoint,
                                    void* data, uint32_t length) {
    // Same as bulk for XHCI
    return xhci_bulk_transfer(dev, endpoint, data, length);
}

// Poll for port changes
static void xhci_poll(usb_controller_t* ctrl) {
    xhci_controller_t* xhci = (xhci_controller_t*)ctrl->driver_data;
    if (!xhci) return;
    
    // Check status register
    uint32_t status = xhci_op_read32(xhci, XHCI_USBSTS);
    
    // Clear status bits
    if (status & (XHCI_STS_EINT | XHCI_STS_PCD | XHCI_STS_HSE)) {
        xhci_op_write32(xhci, XHCI_USBSTS, status & (XHCI_STS_EINT | XHCI_STS_PCD | XHCI_STS_HSE));
    }
    
    // Process event ring
    while (1) {
        xhci_trb_t* event = &xhci->event_ring[xhci->event_ring_index];
        
        uint8_t cycle = event->control & XHCI_TRB_CYCLE;
        if (cycle != xhci->event_ring_cycle) {
            break;
        }
        
        uint8_t type = (event->control >> 10) & 0x3F;
        
        if (type == XHCI_TRB_PORT_STATUS_CHANGE) {
            uint8_t port = ((event->parameter >> 24) & 0xFF) - 1;
            
            if (port < xhci->num_ports) {
                uint32_t port_status = xhci_port_read(xhci, port);
                
                // Clear change bits
                uint32_t change_bits = XHCI_PORT_CSC | XHCI_PORT_PEC | 
                                       XHCI_PORT_WRC | XHCI_PORT_OCC |
                                       XHCI_PORT_PRC | XHCI_PORT_PLC | XHCI_PORT_CEC;
                xhci_port_write(xhci, port, port_status | change_bits);
                
                if (port_status & XHCI_PORT_CSC) {
                    if (port_status & XHCI_PORT_CCS) {
                        tty_putstr("XHCI: Device connected on port ");
                        tty_putdec(port);
                        tty_putstr("\n");
                        
                        xhci_reset_port(ctrl, port);
                    } else {
                        tty_putstr("XHCI: Device disconnected from port ");
                        tty_putdec(port);
                        tty_putstr("\n");
                        
                        xhci->port_connected[port] = 0;
                        xhci->port_slot[port] = 0;
                    }
                }
            }
        }
        
        // Advance event ring
        xhci->event_ring_index++;
        if (xhci->event_ring_index >= EVENT_RING_SIZE) {
            xhci->event_ring_index = 0;
            xhci->event_ring_cycle ^= 1;
        }
    }
    
    // Update ERDP
    uint64_t erdp = xhci_phys(&xhci->event_ring[xhci->event_ring_index]);
    erdp |= (1 << 3);
    xhci_rt_write64(xhci, 0x20 + 0x38, erdp);
}

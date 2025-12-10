//
// E1000 Network Card Driver Implementation
// Intel 82540EM Gigabit Ethernet Controller (QEMU default)
//

#include "e1000.h"
#include "net.h"
#include "tty.h"
#include "kmalloc.h"
#include "../../cpu/ports.h"
#include <stddef.h>

// =============================================================================
// DRIVER STATE
// =============================================================================

static struct {
    int found;                      // Card found flag
    uint32_t mmio_base;             // Memory-mapped I/O base address
    uint8_t irq;                    // IRQ line
    
    // Descriptors (aligned to 16 bytes)
    e1000_rx_desc_t* rx_descs;
    e1000_tx_desc_t* tx_descs;
    
    // RX buffers
    uint8_t* rx_buffers[E1000_NUM_RX_DESC];
    
    // Current descriptor indices
    uint32_t rx_cur;
    uint32_t tx_cur;
    
    // Network interface
    net_interface_t iface;
} e1000_state;

// =============================================================================
// PCI FUNCTIONS
// =============================================================================

static uint32_t pci_read_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (1 << 31) |           // Enable bit
                       ((uint32_t)bus << 16) |
                       ((uint32_t)slot << 11) |
                       ((uint32_t)func << 8) |
                       (offset & 0xFC);
    
    outl(PCI_CONFIG_ADDR, address);
    return inl(PCI_CONFIG_DATA);
}

static void pci_write_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value) {
    uint32_t address = (1 << 31) |
                       ((uint32_t)bus << 16) |
                       ((uint32_t)slot << 11) |
                       ((uint32_t)func << 8) |
                       (offset & 0xFC);
    
    outl(PCI_CONFIG_ADDR, address);
    outl(PCI_CONFIG_DATA, value);
}

static uint16_t pci_read_config16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t val = pci_read_config(bus, slot, func, offset & 0xFC);
    return (val >> ((offset & 2) * 8)) & 0xFFFF;
}

// =============================================================================
// MMIO ACCESS
// =============================================================================

static void e1000_write_reg(uint32_t reg, uint32_t value) {
    volatile uint32_t* addr = (volatile uint32_t*)(uintptr_t)(e1000_state.mmio_base + reg);
    *addr = value;
}

static uint32_t e1000_read_reg(uint32_t reg) {
    volatile uint32_t* addr = (volatile uint32_t*)(uintptr_t)(e1000_state.mmio_base + reg);
    return *addr;
}

// =============================================================================
// EEPROM ACCESS
// =============================================================================

static uint16_t e1000_read_eeprom(uint8_t addr) {
    uint32_t val;
    
    // Start EEPROM read
    e1000_write_reg(E1000_EERD, (addr << E1000_EERD_ADDR_SHIFT) | E1000_EERD_START);
    
    // Wait for completion
    while (!((val = e1000_read_reg(E1000_EERD)) & E1000_EERD_DONE)) {
        // Busy wait
    }
    
    return (val >> E1000_EERD_DATA_SHIFT) & 0xFFFF;
}

// =============================================================================
// MAC ADDRESS
// =============================================================================

static void e1000_read_mac_address(void) {
    // Try reading from EEPROM first
    uint16_t mac01 = e1000_read_eeprom(0);
    uint16_t mac23 = e1000_read_eeprom(1);
    uint16_t mac45 = e1000_read_eeprom(2);
    
    e1000_state.iface.mac.addr[0] = mac01 & 0xFF;
    e1000_state.iface.mac.addr[1] = (mac01 >> 8) & 0xFF;
    e1000_state.iface.mac.addr[2] = mac23 & 0xFF;
    e1000_state.iface.mac.addr[3] = (mac23 >> 8) & 0xFF;
    e1000_state.iface.mac.addr[4] = mac45 & 0xFF;
    e1000_state.iface.mac.addr[5] = (mac45 >> 8) & 0xFF;
}

// =============================================================================
// INITIALIZATION
// =============================================================================

static void e1000_init_rx(void) {
    // Allocate RX descriptors (must be 16-byte aligned)
    // Use static buffer already aligned to 4096 (also satisfies 16-byte requirement)
    static e1000_rx_desc_t rx_desc_array[E1000_NUM_RX_DESC] __attribute__((aligned(4096)));
    e1000_state.rx_descs = rx_desc_array;
    
    // Allocate RX buffers
    static uint8_t rx_buffer_pool[E1000_NUM_RX_DESC * E1000_RX_BUFFER_SIZE] __attribute__((aligned(4096)));
    
    for (int i = 0; i < E1000_NUM_RX_DESC; i++) {
        e1000_state.rx_buffers[i] = &rx_buffer_pool[i * E1000_RX_BUFFER_SIZE];
        e1000_state.rx_descs[i].addr = (uint64_t)(uintptr_t)e1000_state.rx_buffers[i];
        e1000_state.rx_descs[i].status = 0;
    }
    
    // Set RX descriptor ring address
    uint64_t rx_desc_addr = (uint64_t)(uintptr_t)e1000_state.rx_descs;
    e1000_write_reg(E1000_RDBAL, (uint32_t)(rx_desc_addr & 0xFFFFFFFF));
    e1000_write_reg(E1000_RDBAH, (uint32_t)(rx_desc_addr >> 32));
    
    // Set RX descriptor ring length (must be 128-byte aligned)
    e1000_write_reg(E1000_RDLEN, E1000_NUM_RX_DESC * sizeof(e1000_rx_desc_t));
    
    // Set head and tail
    e1000_write_reg(E1000_RDH, 0);
    e1000_write_reg(E1000_RDT, E1000_NUM_RX_DESC - 1);
    
    e1000_state.rx_cur = 0;
    
    // Enable receiver with promiscuous mode for debugging
    uint32_t rctl = E1000_RCTL_EN |
                    E1000_RCTL_SBP |
                    E1000_RCTL_UPE |
                    E1000_RCTL_MPE |
                    E1000_RCTL_LBM_NONE |
                    E1000_RCTL_RDMTS_HALF |
                    E1000_RCTL_BAM |
                    E1000_RCTL_SECRC |
                    E1000_RCTL_BSIZE_2048;
    
    e1000_write_reg(E1000_RCTL, rctl);
}

static void e1000_init_tx(void) {
    // Allocate TX descriptors (must be 16-byte aligned)
    static e1000_tx_desc_t tx_desc_array[E1000_NUM_TX_DESC] __attribute__((aligned(4096)));
    e1000_state.tx_descs = tx_desc_array;
    
    // Initialize TX descriptors
    for (int i = 0; i < E1000_NUM_TX_DESC; i++) {
        e1000_state.tx_descs[i].addr = 0;
        e1000_state.tx_descs[i].cmd = 0;
        e1000_state.tx_descs[i].status = E1000_TXD_STAT_DD;  // Mark as done initially
    }
    
    // Set TX descriptor ring address
    uint64_t tx_desc_addr = (uint64_t)(uintptr_t)e1000_state.tx_descs;
    e1000_write_reg(E1000_TDBAL, (uint32_t)(tx_desc_addr & 0xFFFFFFFF));
    e1000_write_reg(E1000_TDBAH, (uint32_t)(tx_desc_addr >> 32));
    
    // Set TX descriptor ring length
    e1000_write_reg(E1000_TDLEN, E1000_NUM_TX_DESC * sizeof(e1000_tx_desc_t));
    
    // Set head and tail
    e1000_write_reg(E1000_TDH, 0);
    e1000_write_reg(E1000_TDT, 0);
    
    e1000_state.tx_cur = 0;
    
    // Set transmit IPG (Inter Packet Gap)
    e1000_write_reg(E1000_TIPG, (10 << E1000_TIPG_IPGT_SHIFT) |
                                 (8 << E1000_TIPG_IPGR1_SHIFT) |
                                 (6 << E1000_TIPG_IPGR2_SHIFT));
    
    // Enable transmitter
    uint32_t tctl = E1000_TCTL_EN |
                    E1000_TCTL_PSP |
                    (15 << E1000_TCTL_CT_SHIFT) |
                    (64 << E1000_TCTL_COLD_SHIFT) |
                    E1000_TCTL_RTLC;
    
    e1000_write_reg(E1000_TCTL, tctl);
}

static int e1000_pci_scan(void) {
    // Scan PCI bus for E1000 card
    for (uint8_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            uint32_t vendor_device = pci_read_config(bus, slot, 0, 0);
            uint16_t vendor = vendor_device & 0xFFFF;
            uint16_t device = (vendor_device >> 16) & 0xFFFF;
            
            if (vendor == 0xFFFF) continue;
            
            if (vendor == E1000_VENDOR_ID && 
                (device == E1000_DEVICE_ID || device == E1000_DEVICE_ID_I217)) {
                
                // Get BAR0 (MMIO base address)
                uint32_t bar0 = pci_read_config(bus, slot, 0, PCI_BAR0);
                e1000_state.mmio_base = bar0 & 0xFFFFFFF0;  // Mask type bits
                
                // Get IRQ
                uint32_t irq_line = pci_read_config(bus, slot, 0, PCI_INTERRUPT_LINE);
                e1000_state.irq = irq_line & 0xFF;
                
                // Enable bus mastering and memory space
                uint32_t cmd = pci_read_config(bus, slot, 0, PCI_COMMAND);
                cmd |= PCI_CMD_MEMORY_SPACE | PCI_CMD_BUS_MASTER;
                pci_write_config(bus, slot, 0, PCI_COMMAND, cmd);
                
                return 0;
            }
        }
    }
    
    return -1;  // Not found
}

// =============================================================================
// DRIVER INTERFACE
// =============================================================================

static int e1000_driver_send(net_interface_t* iface, const void* data, size_t len) {
    (void)iface;
    return e1000_send(iface, data, len);
}

static void e1000_driver_receive(net_interface_t* iface) {
    (void)iface;
    e1000_poll();
}

int e1000_init(void) {
    // Clear state
    e1000_state.found = 0;
    e1000_state.mmio_base = 0;
    e1000_state.rx_descs = NULL;
    e1000_state.tx_descs = NULL;
    
    // Scan PCI bus for E1000
    if (e1000_pci_scan() != 0) {
        tty_putstr("E1000: No network card found\n");
        return -1;
    }
    
    e1000_state.found = 1;
    
    // Reset the device
    e1000_write_reg(E1000_CTRL, E1000_CTRL_RST);
    
    // Wait for reset to complete
    for (volatile int i = 0; i < 100000; i++);
    
    // Disable interrupts
    e1000_write_reg(E1000_IMC, 0xFFFFFFFF);
    
    // Read MAC address
    e1000_read_mac_address();
    
    char mac_str[18];
    mac_to_string(&e1000_state.iface.mac, mac_str);
    // Set up receive address
    uint32_t ral = e1000_state.iface.mac.addr[0] |
                   (e1000_state.iface.mac.addr[1] << 8) |
                   (e1000_state.iface.mac.addr[2] << 16) |
                   (e1000_state.iface.mac.addr[3] << 24);
    uint32_t rah = e1000_state.iface.mac.addr[4] |
                   (e1000_state.iface.mac.addr[5] << 8) |
                   E1000_RAH_AV;
    
    e1000_write_reg(E1000_RAL0, ral);
    e1000_write_reg(E1000_RAH0, rah);
    
    // Clear multicast table
    for (int i = 0; i < 128; i++) {
        e1000_write_reg(E1000_MTA + i * 4, 0);
    }
    
    // Initialize RX and TX
    e1000_init_rx();
    e1000_init_tx();
    
    // Enable interrupts
    e1000_write_reg(E1000_IMS, E1000_INT_RXT0 | E1000_INT_LSC);
    
    // Link up
    uint32_t ctrl = e1000_read_reg(E1000_CTRL);
    ctrl |= E1000_CTRL_SLU;
    e1000_write_reg(E1000_CTRL, ctrl);
    
    // Check link status
    uint32_t status = e1000_read_reg(E1000_STATUS);
    if (status & E1000_STATUS_LU) {
        // Link is up
    } else {
        tty_putstr("E1000: Link DOWN\n");
    }
    
    // Set up interface structure
    e1000_state.iface.name[0] = 'e';
    e1000_state.iface.name[1] = 't';
    e1000_state.iface.name[2] = 'h';
    e1000_state.iface.name[3] = '0';
    e1000_state.iface.name[4] = '\0';
    
    // Default IP configuration (can be changed via DHCP or static config)
    e1000_state.iface.ip = IP_ADDR(10, 0, 2, 15);       // QEMU default
    e1000_state.iface.netmask = IP_ADDR(255, 255, 255, 0);
    e1000_state.iface.gateway = IP_ADDR(10, 0, 2, 2);   // QEMU default gateway
    
    e1000_state.iface.send = e1000_driver_send;
    e1000_state.iface.receive = e1000_driver_receive;
    e1000_state.iface.driver_data = &e1000_state;
    
    e1000_state.iface.tx_packets = 0;
    e1000_state.iface.rx_packets = 0;
    e1000_state.iface.tx_bytes = 0;
    e1000_state.iface.rx_bytes = 0;
    e1000_state.iface.tx_errors = 0;
    e1000_state.iface.rx_errors = 0;
    
    // Register with network stack
    net_register_interface(&e1000_state.iface);
    
    return 0;
}

net_interface_t* e1000_get_interface(void) {
    if (!e1000_state.found) return NULL;
    return &e1000_state.iface;
}

// =============================================================================
// SEND PACKET
// =============================================================================

int e1000_send(net_interface_t* iface, const void* data, size_t len) {
    (void)iface;
    
    if (!e1000_state.found || !data || len == 0 || len > ETH_FRAME_MAX_SIZE) {
        return -1;
    }
    
    // Get current TX descriptor
    uint32_t cur = e1000_state.tx_cur;
    e1000_tx_desc_t* desc = &e1000_state.tx_descs[cur];
    
    // Wait for descriptor to be available
    while (!(desc->status & E1000_TXD_STAT_DD)) {
        // Busy wait (with timeout in production)
    }
    
    // Copy data to TX buffer (we'll use the data directly)
    // In production, you'd want a proper buffer pool
    static uint8_t tx_buffer[ETH_FRAME_MAX_SIZE] __attribute__((aligned(16)));
    
    for (size_t i = 0; i < len; i++) {
        tx_buffer[i] = ((const uint8_t*)data)[i];
    }
    
    // Set up descriptor
    desc->addr = (uint64_t)(uintptr_t)tx_buffer;
    desc->length = len;
    desc->cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS | E1000_TXD_CMD_RS;
    desc->status = 0;
    
    // Update tail to send packet
    e1000_state.tx_cur = (cur + 1) % E1000_NUM_TX_DESC;
    e1000_write_reg(E1000_TDT, e1000_state.tx_cur);
    
    // Wait for transmission to complete
    while (!(desc->status & E1000_TXD_STAT_DD)) {
        // Busy wait
    }
    
    return 0;
}

// =============================================================================
// RECEIVE PACKETS (POLLING)
// =============================================================================

void e1000_poll(void) {
    if (!e1000_state.found) return;
    
    while (1) {
        uint32_t cur = e1000_state.rx_cur;
        e1000_rx_desc_t* desc = &e1000_state.rx_descs[cur];
        
        // Check if packet is ready
        if (!(desc->status & E1000_RXD_STAT_DD)) {
            break;  // No more packets
        }
        
        // Check for complete packet
        if (desc->status & E1000_RXD_STAT_EOP) {
            // Process packet
            uint8_t* buffer = e1000_state.rx_buffers[cur];
            size_t len = desc->length;
            
            // Pass to network stack
            net_receive_ethernet(&e1000_state.iface, buffer, len);
        }
        
        // Reset descriptor for reuse
        desc->status = 0;
        
        // Update tail
        uint32_t old_cur = cur;
        e1000_state.rx_cur = (cur + 1) % E1000_NUM_RX_DESC;
        e1000_write_reg(E1000_RDT, old_cur);
    }
}

// =============================================================================
// INTERRUPT HANDLER
// =============================================================================

void e1000_interrupt_handler(void) {
    if (!e1000_state.found) return;
    
    // Read and clear interrupt cause
    uint32_t icr = e1000_read_reg(E1000_ICR);
    
    if (icr & E1000_INT_RXT0) {
        // Packet received
        e1000_poll();
    }
    
    if (icr & E1000_INT_LSC) {
        // Link status changed
        uint32_t status = e1000_read_reg(E1000_STATUS);
        if (status & E1000_STATUS_LU) {
            // Link is up
        } else {
            tty_putstr("E1000: Link DOWN\n");
        }
    }
}

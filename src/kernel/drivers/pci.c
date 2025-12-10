//
// PCI Bus Driver Implementation
//

#include "../includes/pci.h"
#include "../../cpu/ports.h"
#include "../includes/tty.h"

// Array of discovered PCI devices
static pci_device_t pci_devices[PCI_MAX_DEVICES];
static int pci_device_count = 0;

// Build PCI configuration address
static uint32_t pci_make_address(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset) {
    return (1 << 31) |          // Enable bit
           ((uint32_t)bus << 16) |
           ((uint32_t)(device & 0x1F) << 11) |
           ((uint32_t)(func & 0x07) << 8) |
           (offset & 0xFC);
}

// Read byte from PCI configuration space
uint8_t pci_read_byte(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset) {
    outl(PCI_CONFIG_ADDRESS, pci_make_address(bus, device, func, offset));
    return inb(PCI_CONFIG_DATA + (offset & 3));
}

// Read word from PCI configuration space
uint16_t pci_read_word(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset) {
    outl(PCI_CONFIG_ADDRESS, pci_make_address(bus, device, func, offset));
    return inw(PCI_CONFIG_DATA + (offset & 2));
}

// Read dword from PCI configuration space
uint32_t pci_read_dword(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset) {
    outl(PCI_CONFIG_ADDRESS, pci_make_address(bus, device, func, offset));
    return inl(PCI_CONFIG_DATA);
}

// Write byte to PCI configuration space
void pci_write_byte(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset, uint8_t value) {
    outl(PCI_CONFIG_ADDRESS, pci_make_address(bus, device, func, offset));
    outb(PCI_CONFIG_DATA + (offset & 3), value);
}

// Write word to PCI configuration space
void pci_write_word(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset, uint16_t value) {
    outl(PCI_CONFIG_ADDRESS, pci_make_address(bus, device, func, offset));
    outw(PCI_CONFIG_DATA + (offset & 2), value);
}

// Write dword to PCI configuration space
void pci_write_dword(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset, uint32_t value) {
    outl(PCI_CONFIG_ADDRESS, pci_make_address(bus, device, func, offset));
    outl(PCI_CONFIG_DATA, value);
}

// Check if a device exists at bus/device/function
static int pci_device_exists(uint8_t bus, uint8_t device, uint8_t func) {
    uint16_t vendor = pci_read_word(bus, device, func, PCI_VENDOR_ID);
    return (vendor != 0xFFFF);
}

// Scan a single function
static void pci_scan_function(uint8_t bus, uint8_t device, uint8_t func) {
    if (pci_device_count >= PCI_MAX_DEVICES) return;
    
    if (!pci_device_exists(bus, device, func)) return;
    
    pci_device_t* dev = &pci_devices[pci_device_count];
    
    dev->bus = bus;
    dev->device = device;
    dev->function = func;
    dev->vendor_id = pci_read_word(bus, device, func, PCI_VENDOR_ID);
    dev->device_id = pci_read_word(bus, device, func, PCI_DEVICE_ID);
    dev->class_code = pci_read_byte(bus, device, func, PCI_CLASS);
    dev->subclass = pci_read_byte(bus, device, func, PCI_SUBCLASS);
    dev->prog_if = pci_read_byte(bus, device, func, PCI_PROG_IF);
    dev->revision = pci_read_byte(bus, device, func, PCI_REVISION_ID);
    dev->irq = pci_read_byte(bus, device, func, PCI_INTERRUPT_LINE);
    
    // Read BARs
    for (int i = 0; i < 6; i++) {
        dev->bar[i] = pci_read_dword(bus, device, func, PCI_BAR0 + i * 4);
    }
    
    pci_device_count++;
}

// Scan a single device (all functions)
static void pci_scan_device(uint8_t bus, uint8_t device) {
    if (!pci_device_exists(bus, device, 0)) return;
    
    pci_scan_function(bus, device, 0);
    
    // Check if multi-function device
    uint8_t header_type = pci_read_byte(bus, device, 0, PCI_HEADER_TYPE);
    if (header_type & 0x80) {
        // Multi-function device
        for (uint8_t func = 1; func < 8; func++) {
            pci_scan_function(bus, device, func);
        }
    }
}

// Scan a single bus
static void pci_scan_bus(uint8_t bus) {
    for (uint8_t device = 0; device < 32; device++) {
        pci_scan_device(bus, device);
    }
}

// Initialize PCI subsystem
void pci_init(void) {
    pci_device_count = 0;
    
    // Check if PCI exists by reading bus 0, device 0
    if (!pci_device_exists(0, 0, 0)) {
        tty_putstr("PCI: No PCI bus detected\n");
        return;
    }
    
    // Check if multi-function device at bus 0, device 0
    uint8_t header_type = pci_read_byte(0, 0, 0, PCI_HEADER_TYPE);
    
    if ((header_type & 0x80) == 0) {
        // Single PCI host controller
        pci_scan_bus(0);
    } else {
        // Multiple PCI host controllers
        for (uint8_t func = 0; func < 8; func++) {
            if (pci_device_exists(0, 0, func)) {
                pci_scan_bus(func);
            }
        }
    }
    
    tty_putstr("PCI: Found ");
    tty_putdec(pci_device_count);
    tty_putstr(" devices\n");
}

// Find device by vendor and device ID
pci_device_t* pci_find_device(uint16_t vendor_id, uint16_t device_id) {
    for (int i = 0; i < pci_device_count; i++) {
        if (pci_devices[i].vendor_id == vendor_id &&
            pci_devices[i].device_id == device_id) {
            return &pci_devices[i];
        }
    }
    return 0;
}

// Find device by class
pci_device_t* pci_find_class(uint8_t class_code, uint8_t subclass, uint8_t prog_if) {
    for (int i = 0; i < pci_device_count; i++) {
        if (pci_devices[i].class_code == class_code &&
            pci_devices[i].subclass == subclass &&
            pci_devices[i].prog_if == prog_if) {
            return &pci_devices[i];
        }
    }
    return 0;
}

// Find next device by class (after prev)
pci_device_t* pci_find_next_class(uint8_t class_code, uint8_t subclass, uint8_t prog_if, pci_device_t* prev) {
    int start = 0;
    if (prev) {
        // Find index of prev
        for (int i = 0; i < pci_device_count; i++) {
            if (&pci_devices[i] == prev) {
                start = i + 1;
                break;
            }
        }
    }
    
    for (int i = start; i < pci_device_count; i++) {
        if (pci_devices[i].class_code == class_code &&
            pci_devices[i].subclass == subclass &&
            pci_devices[i].prog_if == prog_if) {
            return &pci_devices[i];
        }
    }
    return 0;
}

// Enable bus mastering
void pci_enable_bus_mastering(pci_device_t* dev) {
    uint16_t cmd = pci_read_word(dev->bus, dev->device, dev->function, PCI_COMMAND);
    cmd |= PCI_COMMAND_MASTER;
    pci_write_word(dev->bus, dev->device, dev->function, PCI_COMMAND, cmd);
}

// Enable memory space access
void pci_enable_memory_space(pci_device_t* dev) {
    uint16_t cmd = pci_read_word(dev->bus, dev->device, dev->function, PCI_COMMAND);
    cmd |= PCI_COMMAND_MEMORY;
    pci_write_word(dev->bus, dev->device, dev->function, PCI_COMMAND, cmd);
}

// Enable I/O space access
void pci_enable_io_space(pci_device_t* dev) {
    uint16_t cmd = pci_read_word(dev->bus, dev->device, dev->function, PCI_COMMAND);
    cmd |= PCI_COMMAND_IO;
    pci_write_word(dev->bus, dev->device, dev->function, PCI_COMMAND, cmd);
}

// Get device count
int pci_get_device_count(void) {
    return pci_device_count;
}

// Get device by index
pci_device_t* pci_get_device(int index) {
    if (index < 0 || index >= pci_device_count) return 0;
    return &pci_devices[index];
}

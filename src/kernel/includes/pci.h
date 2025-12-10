//
// PCI Bus Driver Header
//

#ifndef PCI_H
#define PCI_H

#include <stdint.h>

// PCI Configuration Space Ports
#define PCI_CONFIG_ADDRESS  0xCF8
#define PCI_CONFIG_DATA     0xCFC

// PCI Configuration Space Registers
#define PCI_VENDOR_ID       0x00
#define PCI_DEVICE_ID       0x02
#define PCI_COMMAND         0x04
#define PCI_STATUS          0x06
#define PCI_REVISION_ID     0x08
#define PCI_PROG_IF         0x09
#define PCI_SUBCLASS        0x0A
#define PCI_CLASS           0x0B
#define PCI_CACHE_LINE_SIZE 0x0C
#define PCI_LATENCY_TIMER   0x0D
#define PCI_HEADER_TYPE     0x0E
#define PCI_BIST            0x0F
#define PCI_BAR0            0x10
#define PCI_BAR1            0x14
#define PCI_BAR2            0x18
#define PCI_BAR3            0x1C
#define PCI_BAR4            0x20
#define PCI_BAR5            0x24
#define PCI_INTERRUPT_LINE  0x3C
#define PCI_INTERRUPT_PIN   0x3D

// PCI Command Register bits
#define PCI_COMMAND_IO          0x0001
#define PCI_COMMAND_MEMORY      0x0002
#define PCI_COMMAND_MASTER      0x0004
#define PCI_COMMAND_INTX_DISABLE 0x0400

// PCI Class Codes
#define PCI_CLASS_SERIAL_BUS    0x0C
#define PCI_SUBCLASS_USB        0x03

// USB Controller Program Interface
#define PCI_USB_PROG_IF_UHCI    0x00
#define PCI_USB_PROG_IF_OHCI    0x10
#define PCI_USB_PROG_IF_EHCI    0x20
#define PCI_USB_PROG_IF_XHCI    0x30

// Maximum devices to track
#define PCI_MAX_DEVICES 64

// PCI Device structure
typedef struct {
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t revision;
    uint8_t irq;
    uint32_t bar[6];
} pci_device_t;

// Initialize PCI subsystem
void pci_init(void);

// Read from PCI configuration space
uint8_t pci_read_byte(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset);
uint16_t pci_read_word(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset);
uint32_t pci_read_dword(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset);

// Write to PCI configuration space
void pci_write_byte(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset, uint8_t value);
void pci_write_word(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset, uint16_t value);
void pci_write_dword(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset, uint32_t value);

// Find PCI devices
pci_device_t* pci_find_device(uint16_t vendor_id, uint16_t device_id);
pci_device_t* pci_find_class(uint8_t class_code, uint8_t subclass, uint8_t prog_if);
pci_device_t* pci_find_next_class(uint8_t class_code, uint8_t subclass, uint8_t prog_if, pci_device_t* prev);

// Enable bus mastering for a device
void pci_enable_bus_mastering(pci_device_t* dev);

// Enable memory space access
void pci_enable_memory_space(pci_device_t* dev);

// Enable I/O space access
void pci_enable_io_space(pci_device_t* dev);

// Get number of discovered devices
int pci_get_device_count(void);

// Get device by index
pci_device_t* pci_get_device(int index);

#endif // PCI_H

//
// UHCI (USB 1.1) Controller Driver
//

#ifndef UHCI_H
#define UHCI_H

#include <stdint.h>
#include "usb.h"
#include "pci.h"

// UHCI I/O Registers (offsets from base)
#define UHCI_USBCMD     0x00    // USB Command
#define UHCI_USBSTS     0x02    // USB Status
#define UHCI_USBINTR    0x04    // USB Interrupt Enable
#define UHCI_FRNUM      0x06    // Frame Number
#define UHCI_FRBASEADD  0x08    // Frame List Base Address
#define UHCI_SOFMOD     0x0C    // Start Of Frame Modify
#define UHCI_PORTSC1    0x10    // Port 1 Status/Control
#define UHCI_PORTSC2    0x12    // Port 2 Status/Control

// USB Command Register bits
#define UHCI_CMD_RS         0x0001  // Run/Stop
#define UHCI_CMD_HCRESET    0x0002  // Host Controller Reset
#define UHCI_CMD_GRESET     0x0004  // Global Reset
#define UHCI_CMD_EGSM       0x0008  // Enter Global Suspend Mode
#define UHCI_CMD_FGR        0x0010  // Force Global Resume
#define UHCI_CMD_SWDBG      0x0020  // Software Debug
#define UHCI_CMD_CF         0x0040  // Configure Flag
#define UHCI_CMD_MAXP       0x0080  // Max Packet (0=32, 1=64)

// USB Status Register bits
#define UHCI_STS_USBINT     0x0001  // USB Interrupt
#define UHCI_STS_ERROR      0x0002  // USB Error Interrupt
#define UHCI_STS_RD         0x0004  // Resume Detect
#define UHCI_STS_HSE        0x0008  // Host System Error
#define UHCI_STS_HCPE       0x0010  // Host Controller Process Error
#define UHCI_STS_HCH        0x0020  // HC Halted

// USB Interrupt Enable bits
#define UHCI_INTR_TIMEOUT   0x0001  // Timeout/CRC
#define UHCI_INTR_RESUME    0x0002  // Resume
#define UHCI_INTR_IOC       0x0004  // Interrupt on Complete
#define UHCI_INTR_SP        0x0008  // Short Packet

// Port Status/Control bits
#define UHCI_PORT_CCS       0x0001  // Current Connect Status
#define UHCI_PORT_CSC       0x0002  // Connect Status Change
#define UHCI_PORT_PED       0x0004  // Port Enabled/Disabled
#define UHCI_PORT_PEDC      0x0008  // Port Enable/Disable Change
#define UHCI_PORT_LSP       0x0010  // Line Status (D+)
#define UHCI_PORT_LSN       0x0020  // Line Status (D-)
#define UHCI_PORT_RD        0x0040  // Resume Detect
#define UHCI_PORT_LSDA      0x0100  // Low Speed Device Attached
#define UHCI_PORT_PR        0x0200  // Port Reset
#define UHCI_PORT_SUSP      0x1000  // Suspend

// Transfer Descriptor (TD) structure
typedef struct __attribute__((packed, aligned(16))) {
    uint32_t link_ptr;      // Link Pointer
    uint32_t ctrl_status;   // Control and Status
    uint32_t token;         // Token
    uint32_t buffer_ptr;    // Buffer Pointer
    // Software use (not read by hardware)
    uint32_t software[4];
} uhci_td_t;

// TD Link Pointer bits
#define UHCI_TD_LINK_TERMINATE  0x0001
#define UHCI_TD_LINK_QH         0x0002  // 1=QH, 0=TD
#define UHCI_TD_LINK_DEPTH      0x0004  // Depth/Breadth select

// TD Control/Status bits
#define UHCI_TD_STATUS_BITSTUFF     (1 << 17)
#define UHCI_TD_STATUS_CRC_TIMEOUT  (1 << 18)
#define UHCI_TD_STATUS_NAK          (1 << 19)
#define UHCI_TD_STATUS_BABBLE       (1 << 20)
#define UHCI_TD_STATUS_DATABUFFER   (1 << 21)
#define UHCI_TD_STATUS_STALLED      (1 << 22)
#define UHCI_TD_STATUS_ACTIVE       (1 << 23)
#define UHCI_TD_STATUS_IOC          (1 << 24)
#define UHCI_TD_STATUS_IOS          (1 << 25)
#define UHCI_TD_STATUS_LS           (1 << 26)
#define UHCI_TD_STATUS_ERRORS       (3 << 27)  // Error counter
#define UHCI_TD_STATUS_SPD          (1 << 29)

// TD Token bits
#define UHCI_TD_TOKEN_PID_SETUP     0x2D
#define UHCI_TD_TOKEN_PID_IN        0x69
#define UHCI_TD_TOKEN_PID_OUT       0xE1

// Queue Head (QH) structure
typedef struct __attribute__((packed, aligned(16))) {
    uint32_t head_link_ptr;     // Queue Head Link Pointer
    uint32_t element_link_ptr;  // Queue Element Link Pointer
    // Software use
    uint32_t software[6];
} uhci_qh_t;

// QH Link Pointer bits (same as TD)
#define UHCI_QH_LINK_TERMINATE  0x0001
#define UHCI_QH_LINK_QH         0x0002

// UHCI Controller structure
typedef struct {
    pci_device_t* pci_dev;
    uint16_t io_base;
    
    // Frame list (1024 entries, 4KB aligned)
    uint32_t* frame_list;
    
    // Queue heads
    uhci_qh_t* qh_control;      // Control transfers
    uhci_qh_t* qh_bulk;         // Bulk transfers
    uhci_qh_t* qh_interrupt[8]; // Interrupt transfers (different polling rates)
    
    // TD pool
    uhci_td_t* td_pool;
    int td_pool_size;
    uint8_t* td_pool_bitmap;
    
    // Connected devices
    usb_device_t* port_device[2];
    
    // Port status
    int port_connected[2];
    int port_enabled[2];
    int port_low_speed[2];
} uhci_controller_t;

// Initialize UHCI controller
int uhci_init(usb_controller_t* ctrl, pci_device_t* pci_dev);

// UHCI controller operations
extern usb_controller_ops_t uhci_ops;

#endif // UHCI_H

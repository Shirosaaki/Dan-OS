//
// EHCI (USB 2.0) Controller Driver Header
//

#ifndef EHCI_H
#define EHCI_H

#include <stdint.h>
#include "usb.h"
#include "pci.h"

// EHCI Capability Registers (offsets from MMIO base)
#define EHCI_CAPLENGTH      0x00    // Capability Register Length
#define EHCI_HCIVERSION     0x02    // Interface Version Number
#define EHCI_HCSPARAMS      0x04    // Structural Parameters
#define EHCI_HCCPARAMS      0x08    // Capability Parameters
#define EHCI_HCSP_PORTROUTE 0x0C    // Companion Port Route Description

// EHCI Operational Registers (offsets from OpBase)
#define EHCI_USBCMD         0x00    // USB Command
#define EHCI_USBSTS         0x04    // USB Status
#define EHCI_USBINTR        0x08    // USB Interrupt Enable
#define EHCI_FRINDEX        0x0C    // Frame Index
#define EHCI_CTRLDSSEGMENT  0x10    // 4G Segment Selector
#define EHCI_PERIODICBASE   0x14    // Periodic Frame List Base Address
#define EHCI_ASYNCLISTADDR  0x18    // Current Asynchronous List Address
#define EHCI_CONFIGFLAG     0x40    // Configure Flag
#define EHCI_PORTSC         0x44    // Port Status/Control (array)

// USB Command Register bits
#define EHCI_CMD_RS         (1 << 0)    // Run/Stop
#define EHCI_CMD_HCRESET    (1 << 1)    // Host Controller Reset
#define EHCI_CMD_FLS_1024   (0 << 2)    // Frame List Size: 1024
#define EHCI_CMD_FLS_512    (1 << 2)    // Frame List Size: 512
#define EHCI_CMD_FLS_256    (2 << 2)    // Frame List Size: 256
#define EHCI_CMD_PSE        (1 << 4)    // Periodic Schedule Enable
#define EHCI_CMD_ASE        (1 << 5)    // Asynchronous Schedule Enable
#define EHCI_CMD_IAAD       (1 << 6)    // Interrupt on Async Advance Doorbell
#define EHCI_CMD_LHCR       (1 << 7)    // Light Host Controller Reset
#define EHCI_CMD_ITC_1      (1 << 16)   // Interrupt Threshold Control

// USB Status Register bits
#define EHCI_STS_USBINT     (1 << 0)    // USB Interrupt
#define EHCI_STS_USBERRINT  (1 << 1)    // USB Error Interrupt
#define EHCI_STS_PCD        (1 << 2)    // Port Change Detect
#define EHCI_STS_FLR        (1 << 3)    // Frame List Rollover
#define EHCI_STS_HSE        (1 << 4)    // Host System Error
#define EHCI_STS_IAA        (1 << 5)    // Interrupt on Async Advance
#define EHCI_STS_HCH        (1 << 12)   // HC Halted
#define EHCI_STS_RECL       (1 << 13)   // Reclamation
#define EHCI_STS_PSS        (1 << 14)   // Periodic Schedule Status
#define EHCI_STS_ASS        (1 << 15)   // Asynchronous Schedule Status

// USB Interrupt Enable bits
#define EHCI_INTR_USBIE     (1 << 0)    // USB Interrupt Enable
#define EHCI_INTR_USBEIE    (1 << 1)    // USB Error Interrupt Enable
#define EHCI_INTR_PCIE      (1 << 2)    // Port Change Interrupt Enable
#define EHCI_INTR_FLRE      (1 << 3)    // Frame List Rollover Enable
#define EHCI_INTR_HSEE      (1 << 4)    // Host System Error Enable
#define EHCI_INTR_IAAE      (1 << 5)    // Interrupt on Async Advance Enable

// Port Status/Control bits
#define EHCI_PORT_CCS       (1 << 0)    // Current Connect Status
#define EHCI_PORT_CSC       (1 << 1)    // Connect Status Change
#define EHCI_PORT_PED       (1 << 2)    // Port Enabled
#define EHCI_PORT_PEDC      (1 << 3)    // Port Enable Change
#define EHCI_PORT_OCA       (1 << 4)    // Over-current Active
#define EHCI_PORT_OCC       (1 << 5)    // Over-current Change
#define EHCI_PORT_FPR       (1 << 6)    // Force Port Resume
#define EHCI_PORT_SUSPEND   (1 << 7)    // Suspend
#define EHCI_PORT_RESET     (1 << 8)    // Port Reset
#define EHCI_PORT_LS        (3 << 10)   // Line Status
#define EHCI_PORT_PP        (1 << 12)   // Port Power
#define EHCI_PORT_OWNER     (1 << 13)   // Port Owner (0=EHCI, 1=Companion)
#define EHCI_PORT_PIC       (3 << 14)   // Port Indicator Control
#define EHCI_PORT_PTC       (15 << 16)  // Port Test Control
#define EHCI_PORT_WKCNNT    (1 << 20)   // Wake on Connect Enable
#define EHCI_PORT_WKDSCNNT  (1 << 21)   // Wake on Disconnect Enable
#define EHCI_PORT_WKOC      (1 << 22)   // Wake on Over-current Enable

// Queue Head Horizontal Link Pointer types
#define EHCI_QH_TYPE_ITD    (0 << 1)    // Isochronous Transfer Descriptor
#define EHCI_QH_TYPE_QH     (1 << 1)    // Queue Head
#define EHCI_QH_TYPE_SITD   (2 << 1)    // Split Transaction ITD
#define EHCI_QH_TYPE_FSTN   (3 << 1)    // Frame Span Traversal Node
#define EHCI_QH_TERMINATE   (1 << 0)    // Terminate

// Queue Element Transfer Descriptor (qTD)
typedef struct __attribute__((packed, aligned(32))) {
    uint32_t next_qtd;          // Next qTD Pointer
    uint32_t alt_next_qtd;      // Alternate Next qTD Pointer
    uint32_t token;             // Token
    uint32_t buffer[5];         // Buffer Pointer List
    uint32_t ext_buffer[5];     // Extended Buffer Pointer (64-bit addressing)
    // Software fields (not used by hardware)
    uint32_t software[3];
} ehci_qtd_t;

// qTD Token bits
#define EHCI_QTD_STATUS_ACTIVE      (1 << 7)
#define EHCI_QTD_STATUS_HALTED      (1 << 6)
#define EHCI_QTD_STATUS_BUFERR      (1 << 5)
#define EHCI_QTD_STATUS_BABBLE      (1 << 4)
#define EHCI_QTD_STATUS_XACTERR     (1 << 3)
#define EHCI_QTD_STATUS_MISSED      (1 << 2)
#define EHCI_QTD_STATUS_SPLITXS     (1 << 1)
#define EHCI_QTD_STATUS_PING        (1 << 0)

#define EHCI_QTD_PID_OUT            (0 << 8)
#define EHCI_QTD_PID_IN             (1 << 8)
#define EHCI_QTD_PID_SETUP          (2 << 8)

#define EHCI_QTD_TOGGLE             (1 << 31)
#define EHCI_QTD_IOC                (1 << 15)

// Queue Head
typedef struct __attribute__((packed, aligned(32))) {
    uint32_t horizontal_link;   // Queue Head Horizontal Link Pointer
    uint32_t endpoint_char;     // Endpoint Characteristics
    uint32_t endpoint_caps;     // Endpoint Capabilities
    uint32_t current_qtd;       // Current qTD Pointer
    // Overlay area (transfer state)
    uint32_t next_qtd;
    uint32_t alt_next_qtd;
    uint32_t token;
    uint32_t buffer[5];
    uint32_t ext_buffer[5];
    // Software fields
    uint32_t software[4];
} ehci_qh_t;

// Endpoint Characteristics bits
#define EHCI_QH_EP_DTC          (1 << 14)   // Data Toggle Control
#define EHCI_QH_EP_HEAD         (1 << 15)   // Head of Reclamation List Flag
#define EHCI_QH_EP_EPS_FULL     (0 << 12)   // Endpoint Speed: Full
#define EHCI_QH_EP_EPS_LOW      (1 << 12)   // Endpoint Speed: Low
#define EHCI_QH_EP_EPS_HIGH     (2 << 12)   // Endpoint Speed: High

// EHCI Controller structure
typedef struct {
    pci_device_t* pci_dev;
    volatile uint8_t* mmio_base;    // Memory-mapped I/O base
    volatile uint8_t* op_base;      // Operational registers base
    
    uint8_t num_ports;
    uint8_t cap_length;
    
    // Periodic frame list (1024 entries, 4KB aligned)
    uint32_t* periodic_list;
    
    // Async queue head (control/bulk)
    ehci_qh_t* async_qh;
    
    // qTD pool
    ehci_qtd_t* qtd_pool;
    int qtd_pool_size;
    uint8_t* qtd_pool_bitmap;
    
    // QH pool
    ehci_qh_t* qh_pool;
    int qh_pool_size;
    uint8_t* qh_pool_bitmap;
    
    // Port devices
    usb_device_t** port_devices;
    int* port_connected;
} ehci_controller_t;

// Initialize EHCI controller
int ehci_init(usb_controller_t* ctrl, pci_device_t* pci_dev);

// EHCI controller operations
extern usb_controller_ops_t ehci_ops;

#endif // EHCI_H

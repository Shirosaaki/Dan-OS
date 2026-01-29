//
// XHCI (USB 3.0) Controller Driver Header
//

#ifndef XHCI_H
#define XHCI_H

#include <stdint.h>
#include <kernel/drivers/usb.h>
#include "pci.h"

// XHCI Capability Registers
#define XHCI_CAPLENGTH      0x00
#define XHCI_HCIVERSION     0x02
#define XHCI_HCSPARAMS1     0x04
#define XHCI_HCSPARAMS2     0x08
#define XHCI_HCSPARAMS3     0x0C
#define XHCI_HCCPARAMS1     0x10
#define XHCI_DBOFF          0x14
#define XHCI_RTSOFF         0x18
#define XHCI_HCCPARAMS2     0x1C

// XHCI Operational Registers (offset from op_base)
#define XHCI_USBCMD         0x00
#define XHCI_USBSTS         0x04
#define XHCI_PAGESIZE       0x08
#define XHCI_DNCTRL         0x14
#define XHCI_CRCR           0x18
#define XHCI_DCBAAP         0x30
#define XHCI_CONFIG         0x38

// Port Register Set (offset from op_base + 0x400)
#define XHCI_PORTSC         0x00
#define XHCI_PORTPMSC       0x04
#define XHCI_PORTLI         0x08
#define XHCI_PORTHLPMC      0x0C

// USB Command Register bits
#define XHCI_CMD_RS         (1 << 0)    // Run/Stop
#define XHCI_CMD_HCRST      (1 << 1)    // Host Controller Reset
#define XHCI_CMD_INTE       (1 << 2)    // Interrupter Enable
#define XHCI_CMD_HSEE       (1 << 3)    // Host System Error Enable
#define XHCI_CMD_LHCRST     (1 << 7)    // Light Host Controller Reset
#define XHCI_CMD_CSS        (1 << 8)    // Controller Save State
#define XHCI_CMD_CRS        (1 << 9)    // Controller Restore State
#define XHCI_CMD_EWE        (1 << 10)   // Enable Wrap Event

// USB Status Register bits
#define XHCI_STS_HCH        (1 << 0)    // HC Halted
#define XHCI_STS_HSE        (1 << 2)    // Host System Error
#define XHCI_STS_EINT       (1 << 3)    // Event Interrupt
#define XHCI_STS_PCD        (1 << 4)    // Port Change Detect
#define XHCI_STS_SSS        (1 << 8)    // Save State Status
#define XHCI_STS_RSS        (1 << 9)    // Restore State Status
#define XHCI_STS_SRE        (1 << 10)   // Save/Restore Error
#define XHCI_STS_CNR        (1 << 11)   // Controller Not Ready
#define XHCI_STS_HCE        (1 << 12)   // Host Controller Error

// Port Status/Control bits
#define XHCI_PORT_CCS       (1 << 0)    // Current Connect Status
#define XHCI_PORT_PED       (1 << 1)    // Port Enabled/Disabled
#define XHCI_PORT_OCA       (1 << 3)    // Over-current Active
#define XHCI_PORT_PR        (1 << 4)    // Port Reset
#define XHCI_PORT_PLS_MASK  (0xF << 5)  // Port Link State
#define XHCI_PORT_PP        (1 << 9)    // Port Power
#define XHCI_PORT_SPEED_MASK (0xF << 10) // Port Speed
#define XHCI_PORT_PIC_MASK  (3 << 14)   // Port Indicator Control
#define XHCI_PORT_LWS       (1 << 16)   // Port Link State Write Strobe
#define XHCI_PORT_CSC       (1 << 17)   // Connect Status Change
#define XHCI_PORT_PEC       (1 << 18)   // Port Enabled/Disabled Change
#define XHCI_PORT_WRC       (1 << 19)   // Warm Port Reset Change
#define XHCI_PORT_OCC       (1 << 20)   // Over-current Change
#define XHCI_PORT_PRC       (1 << 21)   // Port Reset Change
#define XHCI_PORT_PLC       (1 << 22)   // Port Link State Change
#define XHCI_PORT_CEC       (1 << 23)   // Port Config Error Change
#define XHCI_PORT_CAS       (1 << 24)   // Cold Attach Status
#define XHCI_PORT_WCE       (1 << 25)   // Wake on Connect Enable
#define XHCI_PORT_WDE       (1 << 26)   // Wake on Disconnect Enable
#define XHCI_PORT_WOE       (1 << 27)   // Wake on Over-current Enable
#define XHCI_PORT_DR        (1 << 30)   // Device Removable
#define XHCI_PORT_WPR       (1 << 31)   // Warm Port Reset

// Port speeds
#define XHCI_SPEED_FULL     1
#define XHCI_SPEED_LOW      2
#define XHCI_SPEED_HIGH     3
#define XHCI_SPEED_SUPER    4

// TRB Types
#define XHCI_TRB_NORMAL         1
#define XHCI_TRB_SETUP          2
#define XHCI_TRB_DATA           3
#define XHCI_TRB_STATUS         4
#define XHCI_TRB_ISOCH          5
#define XHCI_TRB_LINK           6
#define XHCI_TRB_EVENT_DATA     7
#define XHCI_TRB_NOOP           8
#define XHCI_TRB_ENABLE_SLOT    9
#define XHCI_TRB_DISABLE_SLOT   10
#define XHCI_TRB_ADDRESS_DEV    11
#define XHCI_TRB_CONFIG_EP      12
#define XHCI_TRB_EVAL_CTX       13
#define XHCI_TRB_RESET_EP       14
#define XHCI_TRB_STOP_EP        15
#define XHCI_TRB_SET_TR_DEQ     16
#define XHCI_TRB_RESET_DEV      17
#define XHCI_TRB_FORCE_EVENT    18
#define XHCI_TRB_NEG_BANDWIDTH  19
#define XHCI_TRB_SET_LT         20
#define XHCI_TRB_GET_BW         21
#define XHCI_TRB_FORCE_HEADER   22
#define XHCI_TRB_NOOP_CMD       23

// Event TRB Types
#define XHCI_TRB_TRANSFER_EVENT     32
#define XHCI_TRB_CMD_COMPLETION     33
#define XHCI_TRB_PORT_STATUS_CHANGE 34
#define XHCI_TRB_BANDWIDTH_REQUEST  35
#define XHCI_TRB_DOORBELL           36
#define XHCI_TRB_HOST_CONTROLLER    37
#define XHCI_TRB_DEVICE_NOTIFY      38
#define XHCI_TRB_MFINDEX_WRAP       39

// TRB Completion Codes
#define XHCI_CC_SUCCESS             1
#define XHCI_CC_DATA_BUFFER_ERROR   2
#define XHCI_CC_BABBLE_DETECTED     3
#define XHCI_CC_USB_TRANSACTION_ERR 4
#define XHCI_CC_TRB_ERROR           5
#define XHCI_CC_STALL_ERROR         6
#define XHCI_CC_SHORT_PACKET        13
#define XHCI_CC_RING_UNDERRUN       14
#define XHCI_CC_RING_OVERRUN        15

// Transfer Request Block (TRB)
typedef struct __attribute__((packed, aligned(16))) {
    uint64_t parameter;
    uint32_t status;
    uint32_t control;
} xhci_trb_t;

// TRB Control field bits
#define XHCI_TRB_CYCLE      (1 << 0)
#define XHCI_TRB_ENT        (1 << 1)
#define XHCI_TRB_ISP        (1 << 2)
#define XHCI_TRB_NSNOOP     (1 << 3)
#define XHCI_TRB_CHAIN      (1 << 4)
#define XHCI_TRB_IOC        (1 << 5)
#define XHCI_TRB_IDT        (1 << 6)
#define XHCI_TRB_TYPE(x)    (((x) & 0x3F) << 10)
#define XHCI_TRB_DIR_IN     (1 << 16)

// Slot Context
typedef struct __attribute__((packed, aligned(32))) {
    uint32_t info1;         // Route String, Speed, etc.
    uint32_t info2;         // Number of ports, root hub port
    uint32_t tt_info;       // TT info for LS/FS devices
    uint32_t state;         // Slot state
    uint32_t reserved[4];
} xhci_slot_ctx_t;

// Endpoint Context
typedef struct __attribute__((packed, aligned(32))) {
    uint32_t info1;         // EP state, mult, max P streams
    uint32_t info2;         // EP type, max burst, max packet
    uint64_t tr_dequeue;    // TR Dequeue Pointer
    uint32_t tx_info;       // Average TRB length, max ESIT
    uint32_t reserved[3];
} xhci_ep_ctx_t;

// Device Context (slot + 31 endpoints)
typedef struct __attribute__((packed, aligned(64))) {
    xhci_slot_ctx_t slot;
    xhci_ep_ctx_t ep[31];
} xhci_device_ctx_t;

// Input Control Context
typedef struct __attribute__((packed, aligned(32))) {
    uint32_t drop_flags;
    uint32_t add_flags;
    uint32_t reserved[6];
} xhci_input_ctrl_ctx_t;

// Input Context
typedef struct __attribute__((packed, aligned(64))) {
    xhci_input_ctrl_ctx_t ctrl;
    xhci_slot_ctx_t slot;
    xhci_ep_ctx_t ep[31];
} xhci_input_ctx_t;

// Event Ring Segment Table Entry
typedef struct __attribute__((packed)) {
    uint64_t ring_base;
    uint32_t ring_size;
    uint32_t reserved;
} xhci_erst_entry_t;

// Interrupter Register Set
typedef struct __attribute__((packed)) {
    uint32_t iman;      // Interrupter Management
    uint32_t imod;      // Interrupter Moderation
    uint32_t erstsz;    // Event Ring Segment Table Size
    uint32_t reserved;
    uint64_t erstba;    // Event Ring Segment Table Base Address
    uint64_t erdp;      // Event Ring Dequeue Pointer
} xhci_interrupter_t;

// XHCI Controller structure
typedef struct {
    pci_device_t* pci_dev;
    volatile uint8_t* mmio_base;
    volatile uint8_t* op_base;
    volatile uint8_t* rt_base;      // Runtime registers
    volatile uint32_t* db_base;     // Doorbell registers
    volatile uint8_t* port_base;    // Port registers
    
    uint8_t cap_length;
    uint8_t num_ports;
    uint8_t num_slots;
    uint8_t context_size;           // 32 or 64 bytes
    
    // Device Context Base Address Array
    uint64_t* dcbaa;
    
    // Command Ring
    xhci_trb_t* cmd_ring;
    uint32_t cmd_ring_index;
    uint8_t cmd_ring_cycle;
    
    // Event Ring
    xhci_trb_t* event_ring;
    xhci_erst_entry_t* erst;
    uint32_t event_ring_index;
    uint8_t event_ring_cycle;
    
    // Scratchpad buffers
    uint64_t* scratchpad_array;
    void** scratchpad_buffers;
    int num_scratchpad;
    
    // Device contexts
    xhci_device_ctx_t** device_contexts;
    
    // Transfer rings for each slot/endpoint
    xhci_trb_t** transfer_rings;
    uint32_t* transfer_ring_index;
    uint8_t* transfer_ring_cycle;
    
    // Port info
    int* port_connected;
    int* port_slot;
} xhci_controller_t;

// Initialize XHCI controller
int xhci_init(usb_controller_t* ctrl, pci_device_t* pci_dev);

// XHCI controller operations
extern usb_controller_ops_t xhci_ops;

#endif // XHCI_H

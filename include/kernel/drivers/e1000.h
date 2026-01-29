//
// E1000 Network Card Driver Header
// Intel 82540EM Gigabit Ethernet Controller (QEMU default)
//

#ifndef E1000_H
#define E1000_H

#include <stdint.h>
#include <kernel/net/net.h>

// =============================================================================
// PCI CONFIGURATION
// =============================================================================

// E1000 PCI Vendor and Device IDs
#define E1000_VENDOR_ID     0x8086  // Intel
#define E1000_DEVICE_ID     0x100E  // 82540EM (QEMU default)
#define E1000_DEVICE_ID_I217 0x153A // I217-LM

// PCI Configuration registers
#define PCI_CONFIG_ADDR     0x0CF8
#define PCI_CONFIG_DATA     0x0CFC

#define PCI_VENDOR_ID       0x00
#define PCI_DEVICE_ID       0x02
#define PCI_COMMAND         0x04
#define PCI_STATUS          0x06
#define PCI_BAR0            0x10
#define PCI_BAR1            0x14
#define PCI_INTERRUPT_LINE  0x3C

// PCI Command register bits
#define PCI_CMD_IO_SPACE        (1 << 0)
#define PCI_CMD_MEMORY_SPACE    (1 << 1)
#define PCI_CMD_BUS_MASTER      (1 << 2)
#define PCI_CMD_INT_DISABLE     (1 << 10)

// =============================================================================
// E1000 REGISTERS
// =============================================================================

// Device Control
#define E1000_CTRL          0x0000
#define E1000_CTRL_FD       (1 << 0)     // Full Duplex
#define E1000_CTRL_LRST     (1 << 3)     // Link Reset
#define E1000_CTRL_ASDE     (1 << 5)     // Auto-Speed Detection Enable
#define E1000_CTRL_SLU      (1 << 6)     // Set Link Up
#define E1000_CTRL_ILOS     (1 << 7)     // Invert Loss-of-Signal
#define E1000_CTRL_RST      (1 << 26)    // Device Reset
#define E1000_CTRL_VME      (1 << 30)    // VLAN Mode Enable
#define E1000_CTRL_PHY_RST  (1 << 31)    // PHY Reset

// Device Status
#define E1000_STATUS        0x0008
#define E1000_STATUS_FD     (1 << 0)     // Full Duplex
#define E1000_STATUS_LU     (1 << 1)     // Link Up
#define E1000_STATUS_SPEED_MASK (3 << 6)
#define E1000_STATUS_SPEED_10   (0 << 6)
#define E1000_STATUS_SPEED_100  (1 << 6)
#define E1000_STATUS_SPEED_1000 (2 << 6)

// EEPROM
#define E1000_EERD          0x0014
#define E1000_EERD_START    (1 << 0)
#define E1000_EERD_DONE     (1 << 4)
#define E1000_EERD_ADDR_SHIFT 8
#define E1000_EERD_DATA_SHIFT 16

// Interrupt registers
#define E1000_ICR           0x00C0  // Interrupt Cause Read
#define E1000_ICS           0x00C8  // Interrupt Cause Set
#define E1000_IMS           0x00D0  // Interrupt Mask Set
#define E1000_IMC           0x00D8  // Interrupt Mask Clear

// Interrupt bits
#define E1000_INT_TXDW      (1 << 0)     // TX Descriptor Written Back
#define E1000_INT_TXQE      (1 << 1)     // TX Queue Empty
#define E1000_INT_LSC       (1 << 2)     // Link Status Change
#define E1000_INT_RXSEQ     (1 << 3)     // RX Sequence Error
#define E1000_INT_RXDMT0    (1 << 4)     // RX Descriptor Minimum Threshold
#define E1000_INT_RXO       (1 << 6)     // RX Overrun
#define E1000_INT_RXT0      (1 << 7)     // RX Timer Interrupt

// Receive Control
#define E1000_RCTL          0x0100
#define E1000_RCTL_EN       (1 << 1)     // Receiver Enable
#define E1000_RCTL_SBP      (1 << 2)     // Store Bad Packets
#define E1000_RCTL_UPE      (1 << 3)     // Unicast Promiscuous Enable
#define E1000_RCTL_MPE      (1 << 4)     // Multicast Promiscuous Enable
#define E1000_RCTL_LPE      (1 << 5)     // Long Packet Enable
#define E1000_RCTL_LBM_NONE (0 << 6)     // Loopback Mode - None
#define E1000_RCTL_RDMTS_HALF (0 << 8)   // RX Desc Min Threshold Size
#define E1000_RCTL_MO_36    (0 << 12)    // Multicast Offset
#define E1000_RCTL_BAM      (1 << 15)    // Broadcast Accept Mode
#define E1000_RCTL_BSIZE_2048 (0 << 16)  // Buffer Size 2048
#define E1000_RCTL_BSIZE_1024 (1 << 16)  // Buffer Size 1024
#define E1000_RCTL_BSIZE_512  (2 << 16)  // Buffer Size 512
#define E1000_RCTL_BSIZE_256  (3 << 16)  // Buffer Size 256
#define E1000_RCTL_SECRC    (1 << 26)    // Strip Ethernet CRC

// Transmit Control
#define E1000_TCTL          0x0400
#define E1000_TCTL_EN       (1 << 1)     // Transmit Enable
#define E1000_TCTL_PSP      (1 << 3)     // Pad Short Packets
#define E1000_TCTL_CT_SHIFT 4            // Collision Threshold
#define E1000_TCTL_COLD_SHIFT 12         // Collision Distance
#define E1000_TCTL_SWXOFF   (1 << 22)    // Software XOFF
#define E1000_TCTL_RTLC     (1 << 24)    // Re-transmit on Late Collision

// Transmit IPG (Inter Packet Gap)
#define E1000_TIPG          0x0410
#define E1000_TIPG_IPGT_SHIFT   0
#define E1000_TIPG_IPGR1_SHIFT  10
#define E1000_TIPG_IPGR2_SHIFT  20

// RX Descriptor registers
#define E1000_RDBAL         0x2800  // RX Descriptor Base Low
#define E1000_RDBAH         0x2804  // RX Descriptor Base High
#define E1000_RDLEN         0x2808  // RX Descriptor Length
#define E1000_RDH           0x2810  // RX Descriptor Head
#define E1000_RDT           0x2818  // RX Descriptor Tail

// TX Descriptor registers
#define E1000_TDBAL         0x3800  // TX Descriptor Base Low
#define E1000_TDBAH         0x3804  // TX Descriptor Base High
#define E1000_TDLEN         0x3808  // TX Descriptor Length
#define E1000_TDH           0x3810  // TX Descriptor Head
#define E1000_TDT           0x3818  // TX Descriptor Tail

// Receive Address registers (for MAC filtering)
#define E1000_RAL0          0x5400  // Receive Address Low
#define E1000_RAH0          0x5404  // Receive Address High
#define E1000_RAH_AV        (1 << 31)    // Address Valid

// Multicast Table Array
#define E1000_MTA           0x5200  // 128 entries

// =============================================================================
// DESCRIPTORS
// =============================================================================

// Receive Descriptor
typedef struct {
    uint64_t addr;          // Buffer address
    uint16_t length;        // Packet length
    uint16_t checksum;      // Packet checksum
    uint8_t status;         // Descriptor status
    uint8_t errors;         // Descriptor errors
    uint16_t special;       // Special field
} __attribute__((packed)) e1000_rx_desc_t;

// RX Descriptor Status bits
#define E1000_RXD_STAT_DD   (1 << 0)     // Descriptor Done
#define E1000_RXD_STAT_EOP  (1 << 1)     // End of Packet

// Transmit Descriptor (Legacy)
typedef struct {
    uint64_t addr;          // Buffer address
    uint16_t length;        // Data length
    uint8_t cso;            // Checksum offset
    uint8_t cmd;            // Command
    uint8_t status;         // Descriptor status
    uint8_t css;            // Checksum start
    uint16_t special;       // Special field
} __attribute__((packed)) e1000_tx_desc_t;

// TX Descriptor Command bits
#define E1000_TXD_CMD_EOP   (1 << 0)     // End of Packet
#define E1000_TXD_CMD_IFCS  (1 << 1)     // Insert FCS
#define E1000_TXD_CMD_IC    (1 << 2)     // Insert Checksum
#define E1000_TXD_CMD_RS    (1 << 3)     // Report Status
#define E1000_TXD_CMD_RPS   (1 << 4)     // Report Packet Sent
#define E1000_TXD_CMD_DEXT  (1 << 5)     // Descriptor Extension
#define E1000_TXD_CMD_VLE   (1 << 6)     // VLAN Packet Enable
#define E1000_TXD_CMD_IDE   (1 << 7)     // Interrupt Delay Enable

// TX Descriptor Status bits
#define E1000_TXD_STAT_DD   (1 << 0)     // Descriptor Done

// =============================================================================
// DRIVER CONFIGURATION
// =============================================================================

#define E1000_NUM_RX_DESC   32
#define E1000_NUM_TX_DESC   8
#define E1000_RX_BUFFER_SIZE 2048

// =============================================================================
// DRIVER FUNCTIONS
// =============================================================================

// Initialize E1000 driver (scans PCI bus for card)
int e1000_init(void);

// Get network interface
net_interface_t* e1000_get_interface(void);

// Send packet
int e1000_send(net_interface_t* iface, const void* data, size_t len);

// Handle interrupt
void e1000_interrupt_handler(void);

// Poll for received packets (alternative to interrupt)
void e1000_poll(void);

#endif // E1000_H

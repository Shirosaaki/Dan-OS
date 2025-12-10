//
// Network Stack Header
// Basic networking structures and definitions
//

#ifndef NET_H
#define NET_H

#include <stdint.h>
#include <stddef.h>

// =============================================================================
// MAC ADDRESS
// =============================================================================

typedef struct {
    uint8_t addr[6];
} __attribute__((packed)) mac_addr_t;

// Broadcast MAC address
extern const mac_addr_t MAC_BROADCAST;

// =============================================================================
// ETHERNET FRAME
// =============================================================================

#define ETH_FRAME_MIN_SIZE  60
#define ETH_FRAME_MAX_SIZE  1518
#define ETH_DATA_MIN_SIZE   46
#define ETH_DATA_MAX_SIZE   1500

// Ethernet types (big endian in frame, we define host order values)
#define ETH_TYPE_IPV4       0x0800
#define ETH_TYPE_ARP        0x0806
#define ETH_TYPE_IPV6       0x86DD

typedef struct {
    mac_addr_t dest;        // Destination MAC address
    mac_addr_t src;         // Source MAC address
    uint16_t type;          // EtherType (big endian)
    uint8_t data[];         // Payload (46-1500 bytes)
} __attribute__((packed)) eth_frame_t;

// =============================================================================
// ARP (Address Resolution Protocol)
// =============================================================================

#define ARP_HTYPE_ETHERNET  1
#define ARP_PTYPE_IPV4      0x0800
#define ARP_OP_REQUEST      1
#define ARP_OP_REPLY        2

typedef struct {
    uint16_t htype;         // Hardware type (1 = Ethernet)
    uint16_t ptype;         // Protocol type (0x0800 = IPv4)
    uint8_t hlen;           // Hardware address length (6 for MAC)
    uint8_t plen;           // Protocol address length (4 for IPv4)
    uint16_t oper;          // Operation (1=request, 2=reply)
    mac_addr_t sha;         // Sender hardware address
    uint32_t spa;           // Sender protocol address
    mac_addr_t tha;         // Target hardware address
    uint32_t tpa;           // Target protocol address
} __attribute__((packed)) arp_packet_t;

// =============================================================================
// IPv4
// =============================================================================

typedef struct {
    uint8_t version_ihl;    // Version (4 bits) + IHL (4 bits)
    uint8_t tos;            // Type of service
    uint16_t total_length;  // Total length
    uint16_t id;            // Identification
    uint16_t flags_frag;    // Flags (3 bits) + Fragment offset (13 bits)
    uint8_t ttl;            // Time to live
    uint8_t protocol;       // Protocol (ICMP=1, TCP=6, UDP=17)
    uint16_t checksum;      // Header checksum
    uint32_t src_ip;        // Source IP address
    uint32_t dst_ip;        // Destination IP address
    uint8_t data[];         // Options + Data
} __attribute__((packed)) ipv4_header_t;

#define IP_PROTO_ICMP   1
#define IP_PROTO_TCP    6
#define IP_PROTO_UDP    17

// =============================================================================
// ICMP (Internet Control Message Protocol)
// =============================================================================

#define ICMP_TYPE_ECHO_REPLY    0
#define ICMP_TYPE_ECHO_REQUEST  8

typedef struct {
    uint8_t type;           // Message type
    uint8_t code;           // Message code
    uint16_t checksum;      // Checksum
    uint16_t id;            // Identifier (for echo)
    uint16_t seq;           // Sequence number (for echo)
    uint8_t data[];         // Data
} __attribute__((packed)) icmp_header_t;

// =============================================================================
// UDP (User Datagram Protocol)
// =============================================================================

typedef struct {
    uint16_t src_port;      // Source port
    uint16_t dst_port;      // Destination port
    uint16_t length;        // Length (header + data)
    uint16_t checksum;      // Checksum
    uint8_t data[];         // Data
} __attribute__((packed)) udp_header_t;

// =============================================================================
// NETWORK INTERFACE
// =============================================================================

typedef struct net_interface {
    char name[8];                           // Interface name (e.g., "eth0")
    mac_addr_t mac;                         // MAC address
    uint32_t ip;                            // IP address
    uint32_t netmask;                       // Network mask
    uint32_t gateway;                       // Default gateway
    
    // Driver functions
    int (*send)(struct net_interface* iface, const void* data, size_t len);
    void (*receive)(struct net_interface* iface);
    
    // Statistics
    uint64_t tx_packets;
    uint64_t rx_packets;
    uint64_t tx_bytes;
    uint64_t rx_bytes;
    uint64_t tx_errors;
    uint64_t rx_errors;
    
    // Driver-specific data
    void* driver_data;
} net_interface_t;

// =============================================================================
// NETWORK STACK FUNCTIONS
// =============================================================================

// Initialize the network stack
void net_init(void);

// Register a network interface
int net_register_interface(net_interface_t* iface);

// Get the primary network interface
net_interface_t* net_get_interface(void);

// Send an Ethernet frame
int net_send_ethernet(net_interface_t* iface, const mac_addr_t* dest, 
                      uint16_t type, const void* data, size_t len);

// Process received Ethernet frame
void net_receive_ethernet(net_interface_t* iface, const void* data, size_t len);

// =============================================================================
// ARP FUNCTIONS
// =============================================================================

// Initialize ARP cache
void arp_init(void);

// Resolve IP to MAC address (may send ARP request)
int arp_resolve(net_interface_t* iface, uint32_t ip, mac_addr_t* mac);

// Process received ARP packet
void arp_receive(net_interface_t* iface, const arp_packet_t* arp);

// Send ARP request
void arp_send_request(net_interface_t* iface, uint32_t target_ip);

// =============================================================================
// IPv4 FUNCTIONS
// =============================================================================

// Send IPv4 packet
int ipv4_send(net_interface_t* iface, uint32_t dst_ip, uint8_t protocol,
              const void* data, size_t len);

// Process received IPv4 packet
void ipv4_receive(net_interface_t* iface, const ipv4_header_t* ip, size_t len);

// Calculate IP checksum
uint16_t ipv4_checksum(const void* data, size_t len);

// =============================================================================
// ICMP FUNCTIONS
// =============================================================================

// Send ICMP echo request (ping)
int icmp_send_echo(net_interface_t* iface, uint32_t dst_ip, 
                   uint16_t id, uint16_t seq, const void* data, size_t len);

// Process received ICMP packet
void icmp_receive(net_interface_t* iface, uint32_t src_ip,
                  const icmp_header_t* icmp, size_t len);

// =============================================================================
// UDP FUNCTIONS
// =============================================================================

// UDP receive callback type
typedef void (*udp_handler_t)(net_interface_t* iface, uint32_t src_ip, 
                              uint16_t src_port, uint16_t dst_port,
                              const void* data, size_t len);

// Bind a UDP port to a handler
int udp_bind(uint16_t port, udp_handler_t handler);

// Send UDP packet
int udp_send(net_interface_t* iface, uint32_t dst_ip, 
             uint16_t src_port, uint16_t dst_port,
             const void* data, size_t len);

// Process received UDP packet
void udp_receive(net_interface_t* iface, uint32_t src_ip,
                 const udp_header_t* udp, size_t len);

// =============================================================================
// UTILITY FUNCTIONS
// =============================================================================

// Convert between host and network byte order
static inline uint16_t htons(uint16_t val) {
    return ((val & 0xFF) << 8) | ((val >> 8) & 0xFF);
}

static inline uint16_t ntohs(uint16_t val) {
    return htons(val);  // Same operation
}

static inline uint32_t htonl(uint32_t val) {
    return ((val & 0xFF) << 24) | ((val & 0xFF00) << 8) |
           ((val >> 8) & 0xFF00) | ((val >> 24) & 0xFF);
}

static inline uint32_t ntohl(uint32_t val) {
    return htonl(val);  // Same operation
}

// Create IP address from octets
static inline uint32_t IP_ADDR(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    return ((uint32_t)a << 24) | ((uint32_t)b << 16) | 
           ((uint32_t)c << 8) | (uint32_t)d;
}

// Compare MAC addresses
int mac_equals(const mac_addr_t* a, const mac_addr_t* b);

// Copy MAC address
void mac_copy(mac_addr_t* dest, const mac_addr_t* src);

// Print IP address to string
void ip_to_string(uint32_t ip, char* buf);

// Print MAC address to string
void mac_to_string(const mac_addr_t* mac, char* buf);

// Ping state tracking
extern volatile int ping_reply_received;
extern volatile uint16_t ping_reply_seq;
extern volatile uint32_t ping_reply_from;

#endif // NET_H

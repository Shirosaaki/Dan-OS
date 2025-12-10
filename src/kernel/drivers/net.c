//
// Network Stack Implementation
// Basic Ethernet, ARP, IPv4, ICMP, UDP support
//

#include "net.h"
#include "tty.h"
#include "string.h"
#include <stddef.h>

// =============================================================================
// GLOBALS
// =============================================================================

const mac_addr_t MAC_BROADCAST = {{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}};

static net_interface_t* primary_iface = NULL;

// ARP Cache
#define ARP_CACHE_SIZE 16
typedef struct {
    uint32_t ip;
    mac_addr_t mac;
    uint8_t valid;
} arp_entry_t;

static arp_entry_t arp_cache[ARP_CACHE_SIZE];

// UDP port bindings
#define MAX_UDP_BINDINGS 16
typedef struct {
    uint16_t port;
    udp_handler_t handler;
} udp_binding_t;

static udp_binding_t udp_bindings[MAX_UDP_BINDINGS];

// Packet ID counter for IPv4
static uint16_t ip_packet_id = 0;

// Ping state tracking
volatile int ping_reply_received = 0;
volatile uint16_t ping_reply_seq = 0;
volatile uint32_t ping_reply_from = 0;

// =============================================================================
// UTILITY FUNCTIONS
// =============================================================================

int mac_equals(const mac_addr_t* a, const mac_addr_t* b) {
    for (int i = 0; i < 6; i++) {
        if (a->addr[i] != b->addr[i]) return 0;
    }
    return 1;
}

void mac_copy(mac_addr_t* dest, const mac_addr_t* src) {
    for (int i = 0; i < 6; i++) {
        dest->addr[i] = src->addr[i];
    }
}

void ip_to_string(uint32_t ip, char* buf) {
    uint8_t a = (ip >> 24) & 0xFF;
    uint8_t b = (ip >> 16) & 0xFF;
    uint8_t c = (ip >> 8) & 0xFF;
    uint8_t d = ip & 0xFF;
    
    int pos = 0;
    
    // First octet
    if (a >= 100) buf[pos++] = '0' + (a / 100);
    if (a >= 10) buf[pos++] = '0' + ((a / 10) % 10);
    buf[pos++] = '0' + (a % 10);
    buf[pos++] = '.';
    
    // Second octet
    if (b >= 100) buf[pos++] = '0' + (b / 100);
    if (b >= 10) buf[pos++] = '0' + ((b / 10) % 10);
    buf[pos++] = '0' + (b % 10);
    buf[pos++] = '.';
    
    // Third octet
    if (c >= 100) buf[pos++] = '0' + (c / 100);
    if (c >= 10) buf[pos++] = '0' + ((c / 10) % 10);
    buf[pos++] = '0' + (c % 10);
    buf[pos++] = '.';
    
    // Fourth octet
    if (d >= 100) buf[pos++] = '0' + (d / 100);
    if (d >= 10) buf[pos++] = '0' + ((d / 10) % 10);
    buf[pos++] = '0' + (d % 10);
    
    buf[pos] = '\0';
}

void mac_to_string(const mac_addr_t* mac, char* buf) {
    const char* hex = "0123456789ABCDEF";
    int pos = 0;
    
    for (int i = 0; i < 6; i++) {
        buf[pos++] = hex[(mac->addr[i] >> 4) & 0xF];
        buf[pos++] = hex[mac->addr[i] & 0xF];
        if (i < 5) buf[pos++] = ':';
    }
    buf[pos] = '\0';
}

// =============================================================================
// NETWORK STACK INITIALIZATION
// =============================================================================

void net_init(void) {
    // Clear ARP cache
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        arp_cache[i].valid = 0;
    }
    
    // Clear UDP bindings
    for (int i = 0; i < MAX_UDP_BINDINGS; i++) {
        udp_bindings[i].port = 0;
        udp_bindings[i].handler = NULL;
    }
    
    primary_iface = NULL;
}

int net_register_interface(net_interface_t* iface) {
    if (!iface) return -1;
    
    // For now, just use the first registered interface as primary
    if (!primary_iface) {
        primary_iface = iface;
    }
    
    return 0;
}

net_interface_t* net_get_interface(void) {
    return primary_iface;
}

// =============================================================================
// ETHERNET
// =============================================================================

int net_send_ethernet(net_interface_t* iface, const mac_addr_t* dest,
                      uint16_t type, const void* data, size_t len) {
    if (!iface || !iface->send || !dest || !data) return -1;
    if (len > ETH_DATA_MAX_SIZE) return -1;
    
    // Build Ethernet frame
    uint8_t frame[ETH_FRAME_MAX_SIZE];
    eth_frame_t* eth = (eth_frame_t*)frame;
    
    // Set destination and source MAC
    mac_copy(&eth->dest, dest);
    mac_copy(&eth->src, &iface->mac);
    
    // Set EtherType (convert to network byte order)
    eth->type = htons(type);
    
    // Copy payload
    uint8_t* payload = frame + sizeof(mac_addr_t) * 2 + sizeof(uint16_t);
    for (size_t i = 0; i < len; i++) {
        payload[i] = ((const uint8_t*)data)[i];
    }
    
    // Calculate total frame size (pad to minimum if needed)
    size_t frame_size = 14 + len;  // 14 = 6 + 6 + 2 (dest + src + type)
    if (frame_size < ETH_FRAME_MIN_SIZE) {
        // Pad with zeros
        for (size_t i = frame_size; i < ETH_FRAME_MIN_SIZE; i++) {
            frame[i] = 0;
        }
        frame_size = ETH_FRAME_MIN_SIZE;
    }
    
    // Send via driver
    int result = iface->send(iface, frame, frame_size);
    
    if (result == 0) {
        iface->tx_packets++;
        iface->tx_bytes += frame_size;
    } else {
        iface->tx_errors++;
    }
    
    return result;
}

void net_receive_ethernet(net_interface_t* iface, const void* data, size_t len) {
    if (!iface || !data || len < 14) return;
    
    const eth_frame_t* eth = (const eth_frame_t*)data;
    
    // Check if frame is for us (unicast, broadcast, or multicast)
    if (!mac_equals(&eth->dest, &iface->mac) && 
        !mac_equals(&eth->dest, &MAC_BROADCAST)) {
        return;  // Not for us
    }
    
    iface->rx_packets++;
    iface->rx_bytes += len;
    
    uint16_t type = ntohs(eth->type);
    const uint8_t* payload = (const uint8_t*)data + 14;
    size_t payload_len = len - 14;
    
    switch (type) {
        case ETH_TYPE_ARP:
            if (payload_len >= sizeof(arp_packet_t)) {
                arp_receive(iface, (const arp_packet_t*)payload);
            }
            break;
            
        case ETH_TYPE_IPV4:
            if (payload_len >= sizeof(ipv4_header_t)) {
                ipv4_receive(iface, (const ipv4_header_t*)payload, payload_len);
            }
            break;
            
        default:
            // Unknown protocol, ignore
            break;
    }
}

// =============================================================================
// ARP
// =============================================================================

void arp_init(void) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        arp_cache[i].valid = 0;
    }
}

static void arp_cache_add(uint32_t ip, const mac_addr_t* mac) {
    // Look for existing entry or empty slot
    int empty_slot = -1;
    
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) {
            // Update existing entry
            mac_copy(&arp_cache[i].mac, mac);
            return;
        }
        if (!arp_cache[i].valid && empty_slot < 0) {
            empty_slot = i;
        }
    }
    
    // Add new entry
    if (empty_slot >= 0) {
        arp_cache[empty_slot].ip = ip;
        mac_copy(&arp_cache[empty_slot].mac, mac);
        arp_cache[empty_slot].valid = 1;
    }
}

static int arp_cache_lookup(uint32_t ip, mac_addr_t* mac) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) {
            mac_copy(mac, &arp_cache[i].mac);
            return 0;
        }
    }
    return -1;  // Not found
}

int arp_resolve(net_interface_t* iface, uint32_t ip, mac_addr_t* mac) {
    // Check cache first
    if (arp_cache_lookup(ip, mac) == 0) {
        return 0;
    }
    
    // Not in cache, send ARP request
    arp_send_request(iface, ip);
    
    // For now, return failure - caller should retry later
    // A real implementation would wait or use callbacks
    return -1;
}

void arp_send_request(net_interface_t* iface, uint32_t target_ip) {
    if (!iface) return;
    
    arp_packet_t arp;
    
    arp.htype = htons(ARP_HTYPE_ETHERNET);
    arp.ptype = htons(ARP_PTYPE_IPV4);
    arp.hlen = 6;
    arp.plen = 4;
    arp.oper = htons(ARP_OP_REQUEST);
    
    mac_copy(&arp.sha, &iface->mac);
    arp.spa = htonl(iface->ip);
    
    // Target hardware address is zero for request
    for (int i = 0; i < 6; i++) {
        arp.tha.addr[i] = 0;
    }
    arp.tpa = htonl(target_ip);
    
    // Send as broadcast
    net_send_ethernet(iface, &MAC_BROADCAST, ETH_TYPE_ARP, &arp, sizeof(arp));
}

void arp_receive(net_interface_t* iface, const arp_packet_t* arp) {
    if (!iface || !arp) return;
    
    // Only handle Ethernet/IPv4 ARP
    if (ntohs(arp->htype) != ARP_HTYPE_ETHERNET ||
        ntohs(arp->ptype) != ARP_PTYPE_IPV4) {
        return;
    }
    
    uint32_t sender_ip = ntohl(arp->spa);
    uint32_t target_ip = ntohl(arp->tpa);
    
    // Add sender to cache
    arp_cache_add(sender_ip, &arp->sha);
    
    // Check if this is for us
    if (target_ip != iface->ip) {
        return;
    }
    
    uint16_t oper = ntohs(arp->oper);
    
    if (oper == ARP_OP_REQUEST) {
        // Send ARP reply
        arp_packet_t reply;
        
        reply.htype = htons(ARP_HTYPE_ETHERNET);
        reply.ptype = htons(ARP_PTYPE_IPV4);
        reply.hlen = 6;
        reply.plen = 4;
        reply.oper = htons(ARP_OP_REPLY);
        
        mac_copy(&reply.sha, &iface->mac);
        reply.spa = htonl(iface->ip);
        
        mac_copy(&reply.tha, &arp->sha);
        reply.tpa = arp->spa;
        
        net_send_ethernet(iface, &arp->sha, ETH_TYPE_ARP, &reply, sizeof(reply));
    }
}

// =============================================================================
// IPv4
// =============================================================================

uint16_t ipv4_checksum(const void* data, size_t len) {
    const uint16_t* ptr = (const uint16_t*)data;
    uint32_t sum = 0;
    
    while (len > 1) {
        sum += *ptr++;
        len -= 2;
    }
    
    // Add odd byte if present
    if (len == 1) {
        sum += *(const uint8_t*)ptr;
    }
    
    // Fold 32-bit sum to 16 bits
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    return (uint16_t)~sum;
}

int ipv4_send(net_interface_t* iface, uint32_t dst_ip, uint8_t protocol,
              const void* data, size_t len) {
    if (!iface || !data) return -1;
    if (len > ETH_DATA_MAX_SIZE - sizeof(ipv4_header_t)) return -1;
    
    // Build IP packet
    uint8_t packet[ETH_DATA_MAX_SIZE];
    ipv4_header_t* ip = (ipv4_header_t*)packet;
    
    // IP header (20 bytes, no options)
    ip->version_ihl = 0x45;  // IPv4, 5 DWORDs (20 bytes)
    ip->tos = 0;
    ip->total_length = htons(sizeof(ipv4_header_t) + len);
    ip->id = htons(ip_packet_id++);
    ip->flags_frag = htons(0x4000);  // Don't fragment
    ip->ttl = 64;
    ip->protocol = protocol;
    ip->checksum = 0;
    ip->src_ip = htonl(iface->ip);
    ip->dst_ip = htonl(dst_ip);
    
    // Calculate header checksum
    ip->checksum = ipv4_checksum(ip, sizeof(ipv4_header_t));
    
    // Copy payload
    uint8_t* payload = packet + sizeof(ipv4_header_t);
    for (size_t i = 0; i < len; i++) {
        payload[i] = ((const uint8_t*)data)[i];
    }
    
    // Determine destination MAC
    mac_addr_t dst_mac;
    uint32_t next_hop = dst_ip;
    
    // Check if destination is on local network
    if ((dst_ip & iface->netmask) != (iface->ip & iface->netmask)) {
        // Use gateway
        next_hop = iface->gateway;
    }
    
    // Resolve MAC address
    if (arp_resolve(iface, next_hop, &dst_mac) != 0) {
        // ARP resolution pending
        return -1;
    }
    
    // Send Ethernet frame
    return net_send_ethernet(iface, &dst_mac, ETH_TYPE_IPV4, 
                             packet, sizeof(ipv4_header_t) + len);
}

void ipv4_receive(net_interface_t* iface, const ipv4_header_t* ip, size_t len) {
    if (!iface || !ip) return;
    
    // Verify version
    if ((ip->version_ihl >> 4) != 4) return;
    
    // Get header length
    size_t header_len = (ip->version_ihl & 0x0F) * 4;
    if (header_len < 20 || len < header_len) return;
    
    // Verify checksum
    if (ipv4_checksum(ip, header_len) != 0) return;
    
    // Check if packet is for us
    uint32_t dst_ip = ntohl(ip->dst_ip);
    if (dst_ip != iface->ip && dst_ip != 0xFFFFFFFF) return;
    
    // Get payload
    const uint8_t* payload = (const uint8_t*)ip + header_len;
    size_t payload_len = ntohs(ip->total_length) - header_len;
    uint32_t src_ip = ntohl(ip->src_ip);
    
    switch (ip->protocol) {
        case IP_PROTO_ICMP:
            if (payload_len >= sizeof(icmp_header_t)) {
                icmp_receive(iface, src_ip, (const icmp_header_t*)payload, payload_len);
            }
            break;
            
        case IP_PROTO_UDP:
            if (payload_len >= sizeof(udp_header_t)) {
                udp_receive(iface, src_ip, (const udp_header_t*)payload, payload_len);
            }
            break;
            
        case IP_PROTO_TCP:
            // TCP not implemented yet
            break;
            
        default:
            break;
    }
}

// =============================================================================
// ICMP
// =============================================================================

int icmp_send_echo(net_interface_t* iface, uint32_t dst_ip,
                   uint16_t id, uint16_t seq, const void* data, size_t len) {
    if (!iface) return -1;
    
    uint8_t packet[ETH_DATA_MAX_SIZE];
    icmp_header_t* icmp = (icmp_header_t*)packet;
    
    icmp->type = ICMP_TYPE_ECHO_REQUEST;
    icmp->code = 0;
    icmp->checksum = 0;
    icmp->id = htons(id);
    icmp->seq = htons(seq);
    
    // Copy data
    if (data && len > 0) {
        uint8_t* payload = packet + sizeof(icmp_header_t);
        for (size_t i = 0; i < len; i++) {
            payload[i] = ((const uint8_t*)data)[i];
        }
    }
    
    size_t total_len = sizeof(icmp_header_t) + len;
    icmp->checksum = ipv4_checksum(packet, total_len);
    
    return ipv4_send(iface, dst_ip, IP_PROTO_ICMP, packet, total_len);
}

void icmp_receive(net_interface_t* iface, uint32_t src_ip,
                  const icmp_header_t* icmp, size_t len) {
    if (!iface || !icmp) return;
    
    // Verify checksum
    if (ipv4_checksum(icmp, len) != 0) return;
    
    if (icmp->type == ICMP_TYPE_ECHO_REQUEST) {
        // Send echo reply
        uint8_t reply[ETH_DATA_MAX_SIZE];
        icmp_header_t* rep = (icmp_header_t*)reply;
        
        rep->type = ICMP_TYPE_ECHO_REPLY;
        rep->code = 0;
        rep->checksum = 0;
        rep->id = icmp->id;
        rep->seq = icmp->seq;
        
        // Copy original data
        size_t data_len = len - sizeof(icmp_header_t);
        const uint8_t* orig_data = (const uint8_t*)icmp + sizeof(icmp_header_t);
        uint8_t* rep_data = reply + sizeof(icmp_header_t);
        
        for (size_t i = 0; i < data_len; i++) {
            rep_data[i] = orig_data[i];
        }
        
        rep->checksum = ipv4_checksum(reply, len);
        
        ipv4_send(iface, src_ip, IP_PROTO_ICMP, reply, len);
    }
    else if (icmp->type == ICMP_TYPE_ECHO_REPLY) {
        // Handle ping reply - set flag for waiting code
        ping_reply_received = 1;
        ping_reply_seq = ntohs(icmp->seq);
        ping_reply_from = src_ip;
    }
}

// =============================================================================
// UDP
// =============================================================================

int udp_bind(uint16_t port, udp_handler_t handler) {
    for (int i = 0; i < MAX_UDP_BINDINGS; i++) {
        if (udp_bindings[i].port == 0) {
            udp_bindings[i].port = port;
            udp_bindings[i].handler = handler;
            return 0;
        }
    }
    return -1;  // No free slots
}

int udp_send(net_interface_t* iface, uint32_t dst_ip,
             uint16_t src_port, uint16_t dst_port,
             const void* data, size_t len) {
    if (!iface) return -1;
    
    uint8_t packet[ETH_DATA_MAX_SIZE];
    udp_header_t* udp = (udp_header_t*)packet;
    
    udp->src_port = htons(src_port);
    udp->dst_port = htons(dst_port);
    udp->length = htons(sizeof(udp_header_t) + len);
    udp->checksum = 0;  // Optional for IPv4
    
    // Copy data
    if (data && len > 0) {
        uint8_t* payload = packet + sizeof(udp_header_t);
        for (size_t i = 0; i < len; i++) {
            payload[i] = ((const uint8_t*)data)[i];
        }
    }
    
    return ipv4_send(iface, dst_ip, IP_PROTO_UDP, 
                     packet, sizeof(udp_header_t) + len);
}

void udp_receive(net_interface_t* iface, uint32_t src_ip,
                 const udp_header_t* udp, size_t len) {
    if (!iface || !udp) return;
    
    uint16_t dst_port = ntohs(udp->dst_port);
    uint16_t src_port = ntohs(udp->src_port);
    
    // Find handler for this port
    for (int i = 0; i < MAX_UDP_BINDINGS; i++) {
        if (udp_bindings[i].port == dst_port && udp_bindings[i].handler) {
            size_t data_len = ntohs(udp->length) - sizeof(udp_header_t);
            const uint8_t* data = (const uint8_t*)udp + sizeof(udp_header_t);
            udp_bindings[i].handler(iface, src_ip, src_port, dst_port, data, data_len);
            return;
        }
    }
}

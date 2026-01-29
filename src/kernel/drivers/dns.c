//
// DNS (Domain Name System) Resolver Implementation
//

#include <kernel/net/dns.h>
#include <kernel/net/net.h>
#include <kernel/drivers/e1000.h>
#include <kernel/sys/tty.h>
#include <kernel/sys/string.h>
#include <stddef.h>

// =============================================================================
// STATE
// =============================================================================

static uint32_t dns_server_ip = 0;  // DNS server IP (set by DHCP or manual)
static dns_cache_entry_t dns_cache[DNS_CACHE_SIZE];
static uint16_t dns_transaction_id = 1;

// Pending DNS query state
static volatile int dns_query_pending = 0;
static volatile int dns_query_success = 0;
static volatile uint32_t dns_resolved_ip = 0;
static uint16_t dns_pending_id = 0;

// =============================================================================
// INITIALIZATION
// =============================================================================

void dns_init(void) {
    // Clear cache
    for (int i = 0; i < DNS_CACHE_SIZE; i++) {
        dns_cache[i].valid = 0;
    }
    
    // Default DNS server: QEMU's built-in DNS proxy
    dns_server_ip = IP_ADDR(10, 0, 2, 3);
}

void dns_set_server(uint32_t ip) {
    dns_server_ip = ip;
}

// =============================================================================
// CACHE
// =============================================================================

static int dns_cache_lookup(const char* hostname, uint32_t* ip) {
    for (int i = 0; i < DNS_CACHE_SIZE; i++) {
        if (dns_cache[i].valid) {
            // Compare hostnames (case-insensitive)
            int match = 1;
            int j = 0;
            while (hostname[j] || dns_cache[i].hostname[j]) {
                char c1 = hostname[j];
                char c2 = dns_cache[i].hostname[j];
                // Convert to lowercase
                if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
                if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
                if (c1 != c2) {
                    match = 0;
                    break;
                }
                j++;
            }
            if (match) {
                *ip = dns_cache[i].ip;
                return 0;
            }
        }
    }
    return -1;
}

static void dns_cache_add(const char* hostname, uint32_t ip) {
    // Find empty slot or oldest entry
    int slot = -1;
    for (int i = 0; i < DNS_CACHE_SIZE; i++) {
        if (!dns_cache[i].valid) {
            slot = i;
            break;
        }
    }
    if (slot < 0) slot = 0;  // Overwrite first entry if cache full
    
    // Copy hostname
    int i = 0;
    while (hostname[i] && i < 63) {
        dns_cache[slot].hostname[i] = hostname[i];
        i++;
    }
    dns_cache[slot].hostname[i] = '\0';
    dns_cache[slot].ip = ip;
    dns_cache[slot].valid = 1;
}

// =============================================================================
// DNS PACKET BUILDING
// =============================================================================

// Encode hostname to DNS format (e.g., "www.google.com" -> "\x03www\x06google\x03com\x00")
static int dns_encode_hostname(const char* hostname, uint8_t* buffer, int max_len) {
    int pos = 0;
    int label_start = 0;
    
    while (1) {
        // Find end of current label
        int label_len = 0;
        while (hostname[label_start + label_len] && hostname[label_start + label_len] != '.') {
            label_len++;
        }
        
        if (label_len == 0) break;
        if (pos + label_len + 1 >= max_len) return -1;
        
        // Write length byte
        buffer[pos++] = label_len;
        
        // Write label
        for (int i = 0; i < label_len; i++) {
            buffer[pos++] = hostname[label_start + i];
        }
        
        label_start += label_len;
        if (hostname[label_start] == '.') label_start++;
        if (!hostname[label_start]) break;
    }
    
    // Null terminator
    buffer[pos++] = 0;
    return pos;
}

// =============================================================================
// DNS QUERY
// =============================================================================

static int dns_send_query(const char* hostname) {
    net_interface_t* iface = net_get_interface();
    if (!iface) {
        tty_putstr("DNS: No network interface\n");
        return -1;
    }
    if (dns_server_ip == 0) {
        tty_putstr("DNS: No DNS server configured\n");
        return -1;
    }
    
    // Debug: show DNS server
    char dns_ip_str[16];
    ip_to_string(dns_server_ip, dns_ip_str);
    tty_putstr("DNS: Querying server ");
    tty_putstr(dns_ip_str);
    tty_putstr("\n");
    
    // Build DNS query packet
    uint8_t packet[512];
    int pos = 0;
    
    // DNS header
    dns_header_t* header = (dns_header_t*)packet;
    dns_pending_id = dns_transaction_id++;
    header->id = htons(dns_pending_id);
    header->flags = htons(DNS_FLAG_RD);  // Recursion desired
    header->qdcount = htons(1);          // One question
    header->ancount = 0;
    header->nscount = 0;
    header->arcount = 0;
    pos += sizeof(dns_header_t);
    
    // Encode hostname
    int name_len = dns_encode_hostname(hostname, &packet[pos], 256);
    if (name_len < 0) return -1;
    pos += name_len;
    
    // Query type (A record)
    packet[pos++] = 0;
    packet[pos++] = DNS_TYPE_A;
    
    // Query class (IN)
    packet[pos++] = 0;
    packet[pos++] = DNS_CLASS_IN;
    
    // Set pending state
    dns_query_pending = 1;
    dns_query_success = 0;
    dns_resolved_ip = 0;
    
    // Send UDP packet
    int ret = udp_send(iface, dns_server_ip, 12345, DNS_PORT, packet, pos);
    if (ret < 0) {
        tty_putstr("DNS: Failed to send UDP packet\n");
    } else {
        tty_putstr("DNS: Query sent, waiting for response...\n");
    }
    return ret;
}

// =============================================================================
// DNS RESPONSE PARSING
// =============================================================================

// Skip a DNS name (handles compression)
static int dns_skip_name(const uint8_t* data, int pos, int max) {
    while (pos < max) {
        uint8_t len = data[pos];
        if (len == 0) {
            return pos + 1;
        } else if ((len & 0xC0) == 0xC0) {
            // Compression pointer
            return pos + 2;
        } else {
            pos += len + 1;
        }
    }
    return -1;
}

void dns_handle_response(const void* data, size_t len) {
    tty_putstr("DNS: Received response!\n");
    
    if (len < sizeof(dns_header_t)) return;
    
    const dns_header_t* header = (const dns_header_t*)data;
    
    // Check if this is our response
    if (ntohs(header->id) != dns_pending_id) {
        tty_putstr("DNS: Wrong transaction ID\n");
        return;
    }
    
    // Check if it's a response
    uint16_t flags = ntohs(header->flags);
    if (!(flags & DNS_FLAG_QR)) {
        tty_putstr("DNS: Not a response\n");
        return;
    }
    
    // Check for errors
    if (flags & DNS_FLAG_RCODE) {
        tty_putstr("DNS: Server returned error\n");
        dns_query_pending = 0;
        return;
    }
    
    const uint8_t* packet = (const uint8_t*)data;
    int pos = sizeof(dns_header_t);
    
    // Skip questions
    uint16_t qdcount = ntohs(header->qdcount);
    for (int i = 0; i < qdcount; i++) {
        pos = dns_skip_name(packet, pos, len);
        if (pos < 0) return;
        pos += 4;  // Skip QTYPE and QCLASS
    }
    
    // Parse answers
    uint16_t ancount = ntohs(header->ancount);
    for (int i = 0; i < ancount; i++) {
        if (pos >= (int)len) break;
        
        // Skip name
        pos = dns_skip_name(packet, pos, len);
        if (pos < 0 || pos + 10 > (int)len) return;
        
        // Read type, class, TTL, rdlength
        uint16_t type = (packet[pos] << 8) | packet[pos + 1];
        // uint16_t class = (packet[pos + 2] << 8) | packet[pos + 3];
        // uint32_t ttl = (packet[pos + 4] << 24) | (packet[pos + 5] << 16) | 
        //                (packet[pos + 6] << 8) | packet[pos + 7];
        uint16_t rdlength = (packet[pos + 8] << 8) | packet[pos + 9];
        pos += 10;
        
        if (type == DNS_TYPE_A && rdlength == 4) {
            // IPv4 address!
            dns_resolved_ip = IP_ADDR(packet[pos], packet[pos + 1], 
                                      packet[pos + 2], packet[pos + 3]);
            dns_query_success = 1;
            dns_query_pending = 0;
            return;
        }
        
        pos += rdlength;
    }
    
    dns_query_pending = 0;
}

// UDP handler for DNS responses
static void dns_udp_handler(net_interface_t* iface, uint32_t src_ip, 
                            uint16_t src_port, uint16_t dst_port,
                            const void* data, size_t len) {
    (void)iface;
    (void)dst_port;
    
    char src_ip_str[16];
    ip_to_string(src_ip, src_ip_str);
    tty_putstr("DNS: UDP packet from ");
    tty_putstr(src_ip_str);
    tty_putstr(":");
    tty_putdec(src_port);
    tty_putstr("\n");
    
    if (src_port == DNS_PORT) {
        dns_handle_response(data, len);
    }
}

// =============================================================================
// PUBLIC API
// =============================================================================

int dns_resolve(const char* hostname, uint32_t* ip_out) {
    // Check if it's already an IP address (simple check)
    int dots = 0;
    int digits = 0;
    for (int i = 0; hostname[i]; i++) {
        if (hostname[i] == '.') dots++;
        else if (hostname[i] >= '0' && hostname[i] <= '9') digits++;
        else break;
    }
    
    // Parse IP if it looks like one (e.g., "8.8.8.8")
    if (dots == 3 && digits > 0) {
        uint32_t ip = 0;
        int num = 0;
        int octet = 0;
        for (int i = 0; hostname[i]; i++) {
            if (hostname[i] == '.') {
                ip = (ip << 8) | (num & 0xFF);
                num = 0;
                octet++;
            } else if (hostname[i] >= '0' && hostname[i] <= '9') {
                num = num * 10 + (hostname[i] - '0');
            }
        }
        ip = (ip << 8) | (num & 0xFF);
        if (octet == 3) {
            *ip_out = ip;
            return 0;
        }
    }
    
    // Check cache first
    if (dns_cache_lookup(hostname, ip_out) == 0) {
        return 0;
    }
    
    // Register UDP handler for DNS responses
    static int handler_registered = 0;
    if (!handler_registered) {
        udp_bind(12345, dns_udp_handler);  // Our source port
        handler_registered = 1;
    }
    
    // Send DNS query
    if (dns_send_query(hostname) != 0) {
        return -1;
    }
    
    // Wait for response (with timeout)
    // ~30 million iterations ≈ 10 seconds on typical QEMU
    for (volatile uint32_t i = 0; i < 30000000; i++) {
        e1000_poll();
        
        if (!dns_query_pending) {
            if (dns_query_success && dns_resolved_ip != 0) {
                *ip_out = dns_resolved_ip;
                dns_cache_add(hostname, dns_resolved_ip);
                return 0;
            }
            return -1;
        }
    }
    
    dns_query_pending = 0;
    return -1;  // Timeout
}

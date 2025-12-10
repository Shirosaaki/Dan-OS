//
// DNS (Domain Name System) Resolver Header
//

#ifndef DNS_H
#define DNS_H

#include <stdint.h>
#include "net.h"

// DNS port
#define DNS_PORT 53

// DNS header flags
#define DNS_FLAG_QR       0x8000  // Query/Response
#define DNS_FLAG_OPCODE   0x7800  // Opcode
#define DNS_FLAG_AA       0x0400  // Authoritative Answer
#define DNS_FLAG_TC       0x0200  // Truncated
#define DNS_FLAG_RD       0x0100  // Recursion Desired
#define DNS_FLAG_RA       0x0080  // Recursion Available
#define DNS_FLAG_RCODE    0x000F  // Response Code

// DNS record types
#define DNS_TYPE_A        1       // IPv4 address
#define DNS_TYPE_NS       2       // Nameserver
#define DNS_TYPE_CNAME    5       // Canonical name
#define DNS_TYPE_SOA      6       // Start of authority
#define DNS_TYPE_PTR      12      // Pointer
#define DNS_TYPE_MX       15      // Mail exchange
#define DNS_TYPE_TXT      16      // Text
#define DNS_TYPE_AAAA     28      // IPv6 address

// DNS class
#define DNS_CLASS_IN      1       // Internet

// DNS header structure
typedef struct {
    uint16_t id;          // Transaction ID
    uint16_t flags;       // Flags
    uint16_t qdcount;     // Number of questions
    uint16_t ancount;     // Number of answers
    uint16_t nscount;     // Number of authority records
    uint16_t arcount;     // Number of additional records
} __attribute__((packed)) dns_header_t;

// DNS cache entry
#define DNS_CACHE_SIZE 16
typedef struct {
    char hostname[64];
    uint32_t ip;
    uint32_t ttl;         // Time to live (we'll ignore timing for now)
    uint8_t valid;
} dns_cache_entry_t;

// Initialize DNS resolver
void dns_init(void);

// Set DNS server IP
void dns_set_server(uint32_t ip);

// Resolve hostname to IP address
// Returns 0 on success, -1 on failure
// Result is stored in *ip_out
int dns_resolve(const char* hostname, uint32_t* ip_out);

// Handle DNS response (called by UDP handler)
void dns_handle_response(const void* data, size_t len);

#endif // DNS_H

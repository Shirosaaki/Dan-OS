//
// HTTP Client Header
//

#ifndef HTTP_H
#define HTTP_H

#include <stdint.h>
#include <stddef.h>

// HTTP response structure
typedef struct {
    int status_code;                // HTTP status code (200, 404, etc.)
    char status_text[32];           // Status text ("OK", "Not Found", etc.)
    
    // Headers
    char content_type[64];          // Content-Type header
    size_t content_length;          // Content-Length header
    int chunked;                    // Transfer-Encoding: chunked
    
    // Body
    char* body;                     // Response body (allocated by caller)
    size_t body_len;                // Actual body length
    size_t body_capacity;           // Buffer capacity
} http_response_t;

// Initialize HTTP response structure
void http_response_init(http_response_t* resp, char* body_buffer, size_t buffer_size);

// Perform HTTP GET request
// Returns 0 on success, -1 on error
// Response is stored in *response
int http_get(const char* host, uint16_t port, const char* path, http_response_t* response);

// Perform HTTPS GET request (with TLS)
// Returns 0 on success, -1 on error
int https_get(const char* host, uint16_t port, const char* path, http_response_t* response);

// Parse HTTP response headers (internal helper, also used by HTTPS)
int http_parse_response(const char* data, size_t len, http_response_t* resp);

// Parse URL into components
// Returns 0 on success
// url: input URL (e.g., "http://example.com/path")
// host: output buffer for hostname
// port: output port number
// path: output buffer for path
// is_https: output flag (1 if HTTPS URL, 0 if HTTP)
int http_parse_url(const char* url, char* host, size_t host_size,
                   uint16_t* port, char* path, size_t path_size,
                   int* is_https);

// Fetch URL (convenience wrapper - supports both HTTP and HTTPS!)
// Parses URL and performs HTTP or HTTPS GET based on URL scheme
// Returns 0 on success, -1 on error
int http_fetch(const char* url, http_response_t* response);

#endif // HTTP_H

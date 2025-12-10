//
// HTTP Client Implementation
//

#include "http.h"
#include "tcp.h"
#include "tls.h"
#include "dns.h"
#include "net.h"
#include "e1000.h"
#include "tty.h"
#include "string.h"
#include <stddef.h>

// =============================================================================
// URL PARSING
// =============================================================================

int http_parse_url(const char* url, char* host, size_t host_size,
                   uint16_t* port, char* path, size_t path_size,
                   int* is_https) {
    // Default values
    *port = 80;
    path[0] = '/';
    path[1] = '\0';
    host[0] = '\0';
    *is_https = 0;
    
    const char* p = url;
    
    // Skip protocol (http:// or https://)
    if (strncmp(p, "http://", 7) == 0) {
        p += 7;
        *port = 80;
        *is_https = 0;
    } else if (strncmp(p, "https://", 8) == 0) {
        p += 8;
        *port = 443;
        *is_https = 1;  // Will warn user - TLS not supported
    }
    
    // Extract host
    size_t host_len = 0;
    while (*p && *p != '/' && *p != ':' && host_len < host_size - 1) {
        host[host_len++] = *p++;
    }
    host[host_len] = '\0';
    
    if (host_len == 0) return -1;
    
    // Check for port
    if (*p == ':') {
        p++;
        int port_num = 0;
        while (*p >= '0' && *p <= '9') {
            port_num = port_num * 10 + (*p - '0');
            p++;
        }
        if (port_num > 0 && port_num <= 65535) {
            *port = port_num;
        }
    }
    
    // Extract path
    if (*p == '/') {
        size_t path_len = 0;
        while (*p && path_len < path_size - 1) {
            path[path_len++] = *p++;
        }
        path[path_len] = '\0';
    }
    
    return 0;
}

// =============================================================================
// HTTP RESPONSE HANDLING
// =============================================================================

void http_response_init(http_response_t* resp, char* body_buffer, size_t buffer_size) {
    resp->status_code = 0;
    resp->status_text[0] = '\0';
    resp->content_type[0] = '\0';
    resp->content_length = 0;
    resp->chunked = 0;
    resp->body = body_buffer;
    resp->body_len = 0;
    resp->body_capacity = buffer_size;
}

// Simple string comparison for headers (case-insensitive)
static int header_match(const char* line, const char* header) {
    while (*header) {
        char c1 = *line++;
        char c2 = *header++;
        // Convert to lowercase
        if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
        if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
        if (c1 != c2) return 0;
    }
    return 1;
}

// Parse integer from string
static int parse_int(const char* s) {
    int num = 0;
    while (*s >= '0' && *s <= '9') {
        num = num * 10 + (*s - '0');
        s++;
    }
    return num;
}

// Parse HTTP response headers
int http_parse_response(const char* data, size_t len, http_response_t* resp) {
    const char* p = data;
    const char* end = data + len;
    
    // Parse status line: "HTTP/1.1 200 OK\r\n"
    if (strncmp(p, "HTTP/", 5) != 0) return -1;
    
    // Skip to status code
    while (p < end && *p != ' ') p++;
    if (p >= end) return -1;
    p++;  // Skip space
    
    // Parse status code
    resp->status_code = parse_int(p);
    
    // Skip to status text
    while (p < end && *p != ' ') p++;
    if (p >= end) return -1;
    p++;  // Skip space
    
    // Copy status text
    int i = 0;
    while (p < end && *p != '\r' && *p != '\n' && i < 31) {
        resp->status_text[i++] = *p++;
    }
    resp->status_text[i] = '\0';
    
    // Skip to end of line
    while (p < end && *p != '\n') p++;
    if (p < end) p++;
    
    // Parse headers
    while (p < end) {
        // Check for end of headers
        if (*p == '\r' || *p == '\n') {
            if (*p == '\r') p++;
            if (p < end && *p == '\n') p++;
            break;
        }
        
        // Parse header line
        const char* line_start = p;
        
        // Find end of line
        while (p < end && *p != '\r' && *p != '\n') p++;
        
        // Check for Content-Length
        if (header_match(line_start, "content-length:")) {
            const char* val = line_start + 15;
            while (*val == ' ') val++;
            resp->content_length = parse_int(val);
        }
        // Check for Content-Type
        else if (header_match(line_start, "content-type:")) {
            const char* val = line_start + 13;
            while (*val == ' ') val++;
            int j = 0;
            while (val < p && j < 63) {
                resp->content_type[j++] = *val++;
            }
            resp->content_type[j] = '\0';
        }
        // Check for Transfer-Encoding: chunked
        else if (header_match(line_start, "transfer-encoding:")) {
            const char* val = line_start + 18;
            while (*val == ' ') val++;
            if (header_match(val, "chunked")) {
                resp->chunked = 1;
            }
        }
        
        // Skip end of line
        if (p < end && *p == '\r') p++;
        if (p < end && *p == '\n') p++;
    }
    
    // Return offset to body
    return p - data;
}

// =============================================================================
// HTTP GET
// =============================================================================

int http_get(const char* host, uint16_t port, const char* path, http_response_t* response) {
    // Resolve hostname to IP
    uint32_t ip;
    tty_putstr("Resolving ");
    tty_putstr(host);
    tty_putstr("...\n");
    
    if (dns_resolve(host, &ip) != 0) {
        tty_putstr("DNS resolution failed\n");
        return -1;
    }
    
    char ip_str[16];
    ip_to_string(ip, ip_str);
    tty_putstr("Connecting to ");
    tty_putstr(ip_str);
    tty_putstr(":");
    tty_putdec(port);
    tty_putstr("...\n");
    
    // Connect via TCP
    int conn = tcp_connect(ip, port);
    if (conn < 0) {
        tty_putstr("TCP connect failed\n");
        return -1;
    }
    
    // Wait for connection with timeout
    for (volatile uint32_t i = 0; i < 50000000; i++) {
        e1000_poll();
        if (tcp_is_connected(conn)) break;
        if (tcp_is_closed(conn)) {
            tty_putstr("Connection refused\n");
            return -1;
        }
    }
    
    if (!tcp_is_connected(conn)) {
        tty_putstr("Connection timeout\n");
        tcp_close(conn);
        return -1;
    }
    
    tty_putstr("Connected! Sending request...\n");
    
    // Build HTTP request
    char request[512];
    int req_len = 0;
    
    // GET /path HTTP/1.1\r\n
    const char* get = "GET ";
    for (int i = 0; get[i]; i++) request[req_len++] = get[i];
    for (int i = 0; path[i]; i++) request[req_len++] = path[i];
    const char* http_ver = " HTTP/1.1\r\n";
    for (int i = 0; http_ver[i]; i++) request[req_len++] = http_ver[i];
    
    // Host: header
    const char* host_hdr = "Host: ";
    for (int i = 0; host_hdr[i]; i++) request[req_len++] = host_hdr[i];
    for (int i = 0; host[i]; i++) request[req_len++] = host[i];
    request[req_len++] = '\r';
    request[req_len++] = '\n';
    
    // User-Agent
    const char* ua = "User-Agent: DanOS/1.0\r\n";
    for (int i = 0; ua[i]; i++) request[req_len++] = ua[i];
    
    // Connection: close
    const char* conn_close = "Connection: close\r\n";
    for (int i = 0; conn_close[i]; i++) request[req_len++] = conn_close[i];
    
    // End of headers
    request[req_len++] = '\r';
    request[req_len++] = '\n';
    request[req_len] = '\0';
    
    // Send request
    if (tcp_send(conn, request, req_len) < 0) {
        tty_putstr("Failed to send request\n");
        tcp_close(conn);
        return -1;
    }
    
    tty_putstr("Receiving response...\n");
    
    // Receive response
    char recv_buffer[4096];
    int recv_total = 0;
    int headers_parsed = 0;
    int body_start = 0;
    
    // Wait for data with timeout
    for (volatile uint32_t timeout = 0; timeout < 100000000; timeout++) {
        e1000_poll();
        
        // Check for data
        int bytes = tcp_recv(conn, recv_buffer + recv_total, 
                            sizeof(recv_buffer) - recv_total - 1);
        if (bytes > 0) {
            recv_total += bytes;
            recv_buffer[recv_total] = '\0';
            timeout = 0;  // Reset timeout on data
            
            // Parse headers if not done yet
            if (!headers_parsed) {
                // Look for end of headers (\r\n\r\n)
                for (int i = 0; i < recv_total - 3; i++) {
                    if (recv_buffer[i] == '\r' && recv_buffer[i+1] == '\n' &&
                        recv_buffer[i+2] == '\r' && recv_buffer[i+3] == '\n') {
                        headers_parsed = 1;
                        body_start = http_parse_response(recv_buffer, recv_total, response);
                        break;
                    }
                }
            }
            
            // Check if we have all the body
            if (headers_parsed && response->content_length > 0) {
                size_t body_received = recv_total - body_start;
                if (body_received >= response->content_length) {
                    break;  // Got everything
                }
            }
        }
        
        // Check if connection closed
        if (tcp_is_closed(conn)) {
            break;
        }
    }
    
    // Copy body to response
    if (headers_parsed && body_start > 0 && recv_total > body_start) {
        size_t body_len = recv_total - body_start;
        if (body_len > response->body_capacity - 1) {
            body_len = response->body_capacity - 1;
        }
        for (size_t i = 0; i < body_len; i++) {
            response->body[i] = recv_buffer[body_start + i];
        }
        response->body[body_len] = '\0';
        response->body_len = body_len;
    }
    
    tcp_close(conn);
    
    return (response->status_code > 0) ? 0 : -1;
}

// =============================================================================
// HTTPS GET (using TLS)
// =============================================================================

int https_get(const char* host, uint16_t port, const char* path, http_response_t* response) {
    // Resolve hostname to IP
    uint32_t ip;
    tty_putstr("Resolving ");
    tty_putstr(host);
    tty_putstr("...\n");
    
    if (dns_resolve(host, &ip) != 0) {
        tty_putstr("DNS resolution failed\n");
        return -1;
    }
    
    char ip_str[16];
    ip_to_string(ip, ip_str);
    tty_putstr("Connecting to ");
    tty_putstr(ip_str);
    tty_putstr(":");
    tty_putdec(port);
    tty_putstr(" (HTTPS)...\n");
    
    // Connect via TCP
    int tcp_conn = tcp_connect(ip, port);
    if (tcp_conn < 0) {
        tty_putstr("TCP connect failed\n");
        return -1;
    }
    
    // Wait for TCP connection
    for (volatile uint32_t i = 0; i < 50000000; i++) {
        e1000_poll();
        if (tcp_is_connected(tcp_conn)) break;
        if (tcp_is_closed(tcp_conn)) {
            tty_putstr("Connection refused\n");
            return -1;
        }
    }
    
    if (!tcp_is_connected(tcp_conn)) {
        tty_putstr("TCP connection timeout\n");
        tcp_close(tcp_conn);
        return -1;
    }
    
    // Initialize TLS
    tls_conn_t tls;
    tls_init(&tls, tcp_conn);
    
    // Perform TLS handshake
    if (tls_handshake(&tls, host) != 0) {
        tty_putstr("TLS handshake failed\n");
        tcp_close(tcp_conn);
        return -1;
    }
    
    tty_putstr("TLS connected! Sending HTTPS request...\n");
    
    // Build HTTP request
    char request[512];
    int req_len = 0;
    
    // GET /path HTTP/1.1\r\n
    const char* get = "GET ";
    for (int i = 0; get[i]; i++) request[req_len++] = get[i];
    for (int i = 0; path[i]; i++) request[req_len++] = path[i];
    const char* http_ver = " HTTP/1.1\r\n";
    for (int i = 0; http_ver[i]; i++) request[req_len++] = http_ver[i];
    
    // Host: header
    const char* host_hdr = "Host: ";
    for (int i = 0; host_hdr[i]; i++) request[req_len++] = host_hdr[i];
    for (int i = 0; host[i]; i++) request[req_len++] = host[i];
    request[req_len++] = '\r';
    request[req_len++] = '\n';
    
    // User-Agent
    const char* ua = "User-Agent: DanOS/1.0\r\n";
    for (int i = 0; ua[i]; i++) request[req_len++] = ua[i];
    
    // Connection: close
    const char* conn_close = "Connection: close\r\n";
    for (int i = 0; conn_close[i]; i++) request[req_len++] = conn_close[i];
    
    // End of headers
    request[req_len++] = '\r';
    request[req_len++] = '\n';
    request[req_len] = '\0';
    
    // Send request via TLS
    if (tls_send(&tls, (uint8_t*)request, req_len) < 0) {
        tty_putstr("Failed to send HTTPS request\n");
        tls_close(&tls);
        tcp_close(tcp_conn);
        return -1;
    }
    
    tty_putstr("Receiving HTTPS response...\n");
    
    // Receive response via TLS
    char recv_buffer[4096];
    int recv_total = 0;
    int headers_parsed = 0;
    int body_start = 0;
    
    // Wait for data with timeout
    for (volatile uint32_t timeout = 0; timeout < 100000000; timeout++) {
        e1000_poll();
        
        int bytes = tls_recv(&tls, (uint8_t*)(recv_buffer + recv_total), 
                            sizeof(recv_buffer) - recv_total - 1);
        if (bytes > 0) {
            recv_total += bytes;
            recv_buffer[recv_total] = '\0';
            timeout = 0;
            
            // Parse headers if not done yet
            if (!headers_parsed) {
                for (int i = 0; i < recv_total - 3; i++) {
                    if (recv_buffer[i] == '\r' && recv_buffer[i+1] == '\n' &&
                        recv_buffer[i+2] == '\r' && recv_buffer[i+3] == '\n') {
                        headers_parsed = 1;
                        body_start = http_parse_response(recv_buffer, recv_total, response);
                        break;
                    }
                }
            }
            
            // Check if we have all the body
            if (headers_parsed && response->content_length > 0) {
                size_t body_received = recv_total - body_start;
                if (body_received >= response->content_length) {
                    break;
                }
            }
        }
        
        if (!tls_is_connected(&tls)) {
            break;
        }
    }
    
    // Copy body to response
    if (headers_parsed && body_start > 0 && recv_total > body_start) {
        size_t body_len = recv_total - body_start;
        if (body_len > response->body_capacity - 1) {
            body_len = response->body_capacity - 1;
        }
        for (size_t i = 0; i < body_len; i++) {
            response->body[i] = recv_buffer[body_start + i];
        }
        response->body[body_len] = '\0';
        response->body_len = body_len;
    }
    
    tls_close(&tls);
    tcp_close(tcp_conn);
    
    return (response->status_code > 0) ? 0 : -1;
}

// =============================================================================
// HTTP FETCH (convenience function - supports HTTP and HTTPS)
// =============================================================================

int http_fetch(const char* url, http_response_t* response) {
    char host[128];
    char path[256];
    uint16_t port;
    int is_https;
    
    // Parse the URL
    if (http_parse_url(url, host, sizeof(host), &port, path, sizeof(path), &is_https) != 0) {
        tty_putstr("Error: Invalid URL format\n");
        return -1;
    }
    
    if (is_https) {
        tty_putstr("Using HTTPS (TLS 1.2)...\n");
        return https_get(host, port, path, response);
    } else {
        return http_get(host, port, path, response);
    }
}

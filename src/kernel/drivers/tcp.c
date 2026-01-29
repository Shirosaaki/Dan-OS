//
// TCP (Transmission Control Protocol) Stack Implementation
//

#include <kernel/net/tcp.h>
#include <kernel/net/net.h>
#include <kernel/drivers/e1000.h>
#include <kernel/sys/tty.h>
#include <stddef.h>

// =============================================================================
// STATE
// =============================================================================

static tcp_connection_t connections[TCP_MAX_CONNECTIONS];
static uint16_t next_local_port = 49152;  // Ephemeral port range start

// =============================================================================
// INITIALIZATION
// =============================================================================

void tcp_init(void) {
    for (int i = 0; i < TCP_MAX_CONNECTIONS; i++) {
        connections[i].active = 0;
        connections[i].state = TCP_STATE_CLOSED;
    }
}

// =============================================================================
// TCP CHECKSUM
// =============================================================================

// TCP pseudo-header for checksum calculation
typedef struct {
    uint32_t src_ip;
    uint32_t dst_ip;
    uint8_t zero;
    uint8_t protocol;
    uint16_t tcp_length;
} __attribute__((packed)) tcp_pseudo_header_t;

static uint16_t tcp_checksum(uint32_t src_ip, uint32_t dst_ip, 
                             const tcp_header_t* tcp, size_t tcp_len) {
    uint32_t sum = 0;
    
    // Pseudo header
    sum += (src_ip >> 16) & 0xFFFF;
    sum += src_ip & 0xFFFF;
    sum += (dst_ip >> 16) & 0xFFFF;
    sum += dst_ip & 0xFFFF;
    sum += htons(IP_PROTO_TCP);
    sum += htons(tcp_len);
    
    // TCP header + data
    const uint8_t* ptr = (const uint8_t*)tcp;
    size_t len = tcp_len;
    
    while (len > 1) {
        sum += (ptr[0] << 8) | ptr[1];
        ptr += 2;
        len -= 2;
    }
    
    if (len == 1) {
        sum += *ptr;
    }
    
    // Fold 32-bit sum to 16 bits
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    return (uint16_t)~sum;
}

// =============================================================================
// SEND TCP SEGMENT
// =============================================================================

static int tcp_send_segment(tcp_connection_t* conn, uint8_t flags, 
                            const void* data, size_t data_len) {
    net_interface_t* iface = net_get_interface();
    if (!iface) return -1;
    
    // Build TCP segment
    uint8_t packet[1500];
    tcp_header_t* tcp = (tcp_header_t*)packet;
    
    tcp->src_port = htons(conn->local_port);
    tcp->dst_port = htons(conn->remote_port);
    tcp->seq_num = htonl(conn->send_seq);
    tcp->ack_num = htonl(conn->recv_seq);
    tcp->data_offset = (5 << 4);  // 20 bytes (no options)
    tcp->flags = flags;
    tcp->window = htons(TCP_BUFFER_SIZE);
    tcp->checksum = 0;
    tcp->urgent_ptr = 0;
    
    // Copy data
    size_t tcp_len = sizeof(tcp_header_t);
    if (data && data_len > 0) {
        if (data_len > 1460) data_len = 1460;  // MSS limit
        for (size_t i = 0; i < data_len; i++) {
            packet[sizeof(tcp_header_t) + i] = ((const uint8_t*)data)[i];
        }
        tcp_len += data_len;
    }
    
    // Calculate checksum
    tcp->checksum = tcp_checksum(htonl(iface->ip), htonl(conn->remote_ip), 
                                 tcp, tcp_len);
    
    // Send via IPv4
    return ipv4_send(iface, conn->remote_ip, IP_PROTO_TCP, packet, tcp_len);
}

// =============================================================================
// CONNECTION MANAGEMENT
// =============================================================================

static tcp_connection_t* tcp_find_connection(uint32_t remote_ip, uint16_t remote_port,
                                              uint16_t local_port) {
    for (int i = 0; i < TCP_MAX_CONNECTIONS; i++) {
        if (connections[i].active &&
            connections[i].remote_ip == remote_ip &&
            connections[i].remote_port == remote_port &&
            connections[i].local_port == local_port) {
            return &connections[i];
        }
    }
    return NULL;
}

static tcp_connection_t* tcp_alloc_connection(void) {
    for (int i = 0; i < TCP_MAX_CONNECTIONS; i++) {
        if (!connections[i].active) {
            connections[i].active = 1;
            connections[i].state = TCP_STATE_CLOSED;
            connections[i].recv_len = 0;
            connections[i].recv_read_pos = 0;
            connections[i].send_len = 0;
            connections[i].data_available = 0;
            connections[i].connection_closed = 0;
            return &connections[i];
        }
    }
    return NULL;
}

// =============================================================================
// PUBLIC API
// =============================================================================

int tcp_connect(uint32_t remote_ip, uint16_t remote_port) {
    tcp_connection_t* conn = tcp_alloc_connection();
    if (!conn) return -1;
    
    // Set up connection
    conn->remote_ip = remote_ip;
    conn->remote_port = remote_port;
    conn->local_port = next_local_port++;
    if (next_local_port > 65535) next_local_port = 49152;
    
    // Initialize sequence numbers (simple, not cryptographically random)
    conn->send_seq = 1000 + (next_local_port * 17);
    conn->recv_seq = 0;
    
    // Send SYN
    conn->state = TCP_STATE_SYN_SENT;
    if (tcp_send_segment(conn, TCP_FLAG_SYN, NULL, 0) != 0) {
        conn->active = 0;
        return -1;
    }
    conn->send_seq++;  // SYN consumes one sequence number
    
    // Return connection index
    for (int i = 0; i < TCP_MAX_CONNECTIONS; i++) {
        if (&connections[i] == conn) return i;
    }
    return -1;
}

int tcp_send(int conn_id, const void* data, size_t len) {
    if (conn_id < 0 || conn_id >= TCP_MAX_CONNECTIONS) return -1;
    tcp_connection_t* conn = &connections[conn_id];
    if (!conn->active || conn->state != TCP_STATE_ESTABLISHED) return -1;
    
    // Send data in chunks
    const uint8_t* ptr = (const uint8_t*)data;
    size_t sent = 0;
    
    while (sent < len) {
        size_t chunk = len - sent;
        if (chunk > 1460) chunk = 1460;  // MSS
        
        if (tcp_send_segment(conn, TCP_FLAG_ACK | TCP_FLAG_PSH, ptr + sent, chunk) != 0) {
            break;
        }
        
        conn->send_seq += chunk;
        sent += chunk;
    }
    
    return sent;
}

int tcp_recv(int conn_id, void* buffer, size_t max_len) {
    if (conn_id < 0 || conn_id >= TCP_MAX_CONNECTIONS) return -1;
    tcp_connection_t* conn = &connections[conn_id];
    if (!conn->active) return -1;
    
    if (conn->recv_len == 0) return 0;
    
    // Copy available data
    size_t to_copy = conn->recv_len - conn->recv_read_pos;
    if (to_copy > max_len) to_copy = max_len;
    
    uint8_t* out = (uint8_t*)buffer;
    for (size_t i = 0; i < to_copy; i++) {
        out[i] = conn->recv_buffer[conn->recv_read_pos + i];
    }
    
    conn->recv_read_pos += to_copy;
    
    // Reset buffer if fully consumed
    if (conn->recv_read_pos >= conn->recv_len) {
        conn->recv_len = 0;
        conn->recv_read_pos = 0;
        conn->data_available = 0;
    }
    
    return to_copy;
}

void tcp_close(int conn_id) {
    if (conn_id < 0 || conn_id >= TCP_MAX_CONNECTIONS) return;
    tcp_connection_t* conn = &connections[conn_id];
    if (!conn->active) return;
    
    if (conn->state == TCP_STATE_ESTABLISHED) {
        // Send FIN
        tcp_send_segment(conn, TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0);
        conn->send_seq++;
        conn->state = TCP_STATE_FIN_WAIT_1;
    } else {
        conn->active = 0;
        conn->state = TCP_STATE_CLOSED;
    }
}

int tcp_is_connected(int conn_id) {
    if (conn_id < 0 || conn_id >= TCP_MAX_CONNECTIONS) return 0;
    return connections[conn_id].active && 
           connections[conn_id].state == TCP_STATE_ESTABLISHED;
}

int tcp_data_available(int conn_id) {
    if (conn_id < 0 || conn_id >= TCP_MAX_CONNECTIONS) return 0;
    return connections[conn_id].data_available;
}

int tcp_is_closed(int conn_id) {
    if (conn_id < 0 || conn_id >= TCP_MAX_CONNECTIONS) return 1;
    return connections[conn_id].connection_closed || 
           !connections[conn_id].active;
}

// =============================================================================
// RECEIVE HANDLING
// =============================================================================

void tcp_receive(net_interface_t* iface, uint32_t src_ip, 
                 const tcp_header_t* tcp, size_t len) {
    (void)iface;
    
    if (len < sizeof(tcp_header_t)) return;
    
    uint16_t src_port = ntohs(tcp->src_port);
    uint16_t dst_port = ntohs(tcp->dst_port);
    uint32_t seq_num = ntohl(tcp->seq_num);
    uint32_t ack_num = ntohl(tcp->ack_num);
    uint8_t flags = tcp->flags;
    
    // Find matching connection
    tcp_connection_t* conn = tcp_find_connection(src_ip, src_port, dst_port);
    if (!conn) {
        // No connection - send RST if not a RST
        if (!(flags & TCP_FLAG_RST)) {
            // TODO: Send RST
        }
        return;
    }
    
    // Get data offset
    int header_len = (tcp->data_offset >> 4) * 4;
    const uint8_t* data = (const uint8_t*)tcp + header_len;
    size_t data_len = len - header_len;
    
    // Handle based on state
    switch (conn->state) {
        case TCP_STATE_SYN_SENT:
            // Expecting SYN+ACK
            if ((flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) == (TCP_FLAG_SYN | TCP_FLAG_ACK)) {
                conn->recv_seq = seq_num + 1;
                conn->recv_ack = ack_num;
                
                // Send ACK
                conn->state = TCP_STATE_ESTABLISHED;
                tcp_send_segment(conn, TCP_FLAG_ACK, NULL, 0);
            }
            break;
            
        case TCP_STATE_ESTABLISHED:
            // Handle incoming data
            if (flags & TCP_FLAG_ACK) {
                conn->recv_ack = ack_num;
            }
            
            if (data_len > 0) {
                // Store received data
                if (conn->recv_len + data_len <= TCP_BUFFER_SIZE) {
                    for (size_t i = 0; i < data_len; i++) {
                        conn->recv_buffer[conn->recv_len++] = data[i];
                    }
                    conn->recv_seq += data_len;
                    conn->data_available = 1;
                    
                    // Send ACK
                    tcp_send_segment(conn, TCP_FLAG_ACK, NULL, 0);
                }
            }
            
            if (flags & TCP_FLAG_FIN) {
                // Remote is closing
                conn->recv_seq++;
                conn->connection_closed = 1;
                tcp_send_segment(conn, TCP_FLAG_ACK, NULL, 0);
                conn->state = TCP_STATE_CLOSE_WAIT;
            }
            break;
            
        case TCP_STATE_FIN_WAIT_1:
            if (flags & TCP_FLAG_ACK) {
                conn->state = TCP_STATE_FIN_WAIT_2;
            }
            if (flags & TCP_FLAG_FIN) {
                conn->recv_seq++;
                tcp_send_segment(conn, TCP_FLAG_ACK, NULL, 0);
                conn->state = TCP_STATE_TIME_WAIT;
                conn->active = 0;  // Simple cleanup
            }
            break;
            
        case TCP_STATE_FIN_WAIT_2:
            if (flags & TCP_FLAG_FIN) {
                conn->recv_seq++;
                tcp_send_segment(conn, TCP_FLAG_ACK, NULL, 0);
                conn->state = TCP_STATE_TIME_WAIT;
                conn->active = 0;  // Simple cleanup
            }
            break;
            
        case TCP_STATE_CLOSE_WAIT:
            // We should send FIN
            tcp_send_segment(conn, TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0);
            conn->send_seq++;
            conn->state = TCP_STATE_LAST_ACK;
            break;
            
        case TCP_STATE_LAST_ACK:
            if (flags & TCP_FLAG_ACK) {
                conn->active = 0;
                conn->state = TCP_STATE_CLOSED;
            }
            break;
            
        default:
            break;
    }
}

void tcp_poll(void) {
    // TODO: Handle retransmissions and timeouts
}

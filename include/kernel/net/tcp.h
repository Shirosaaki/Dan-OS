//
// TCP (Transmission Control Protocol) Stack Header
//

#ifndef TCP_H
#define TCP_H

#include <stdint.h>
#include <kernel/net/net.h>

// TCP header structure
typedef struct {
    uint16_t src_port;      // Source port
    uint16_t dst_port;      // Destination port
    uint32_t seq_num;       // Sequence number
    uint32_t ack_num;       // Acknowledgment number
    uint8_t data_offset;    // Data offset (header length in 32-bit words) << 4
    uint8_t flags;          // Control flags
    uint16_t window;        // Window size
    uint16_t checksum;      // Checksum
    uint16_t urgent_ptr;    // Urgent pointer
} __attribute__((packed)) tcp_header_t;

// TCP flags
#define TCP_FLAG_FIN    0x01
#define TCP_FLAG_SYN    0x02
#define TCP_FLAG_RST    0x04
#define TCP_FLAG_PSH    0x08
#define TCP_FLAG_ACK    0x10
#define TCP_FLAG_URG    0x20

// TCP connection states
typedef enum {
    TCP_STATE_CLOSED,
    TCP_STATE_LISTEN,
    TCP_STATE_SYN_SENT,
    TCP_STATE_SYN_RECEIVED,
    TCP_STATE_ESTABLISHED,
    TCP_STATE_FIN_WAIT_1,
    TCP_STATE_FIN_WAIT_2,
    TCP_STATE_CLOSE_WAIT,
    TCP_STATE_CLOSING,
    TCP_STATE_LAST_ACK,
    TCP_STATE_TIME_WAIT
} tcp_state_t;

// TCP connection (socket)
#define TCP_MAX_CONNECTIONS 8
#define TCP_BUFFER_SIZE 4096

typedef struct {
    int active;                     // Is this slot in use?
    tcp_state_t state;              // Connection state
    
    // Local endpoint
    uint16_t local_port;
    
    // Remote endpoint
    uint32_t remote_ip;
    uint16_t remote_port;
    
    // Sequence numbers
    uint32_t send_seq;              // Our sequence number
    uint32_t send_ack;              // What we've acked
    uint32_t recv_seq;              // Their sequence number
    uint32_t recv_ack;              // What they've acked
    
    // Receive buffer
    uint8_t recv_buffer[TCP_BUFFER_SIZE];
    int recv_len;
    int recv_read_pos;
    
    // Send buffer
    uint8_t send_buffer[TCP_BUFFER_SIZE];
    int send_len;
    
    // Flags
    int data_available;             // New data received
    int connection_closed;          // Remote closed connection
} tcp_connection_t;

// Initialize TCP stack
void tcp_init(void);

// Create a new TCP connection (returns connection index, or -1 on error)
int tcp_connect(uint32_t remote_ip, uint16_t remote_port);

// Send data on a connection
int tcp_send(int conn_id, const void* data, size_t len);

// Receive data from a connection (non-blocking, returns bytes read)
int tcp_recv(int conn_id, void* buffer, size_t max_len);

// Close a connection
void tcp_close(int conn_id);

// Check if connection is established
int tcp_is_connected(int conn_id);

// Check if data is available
int tcp_data_available(int conn_id);

// Check if connection closed by remote
int tcp_is_closed(int conn_id);

// Process received TCP packet (called by IPv4 handler)
void tcp_receive(net_interface_t* iface, uint32_t src_ip, 
                 const tcp_header_t* tcp, size_t len);

// Poll TCP (handle timeouts, retransmissions)
void tcp_poll(void);

#endif // TCP_H

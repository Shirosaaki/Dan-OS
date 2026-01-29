//
// TLS 1.2 Implementation
//

#ifndef TLS_H
#define TLS_H

#include <stdint.h>
#include <stddef.h>
#include <kernel/drivers/aes.h>
#include <kernel/drivers/sha256.h>
#include <kernel/drivers/rsa.h>

// TLS versions
#define TLS_VERSION_1_0 0x0301
#define TLS_VERSION_1_1 0x0302
#define TLS_VERSION_1_2 0x0303

// Content types
#define TLS_CONTENT_CHANGE_CIPHER_SPEC 20
#define TLS_CONTENT_ALERT              21
#define TLS_CONTENT_HANDSHAKE          22
#define TLS_CONTENT_APPLICATION_DATA   23

// Handshake types
#define TLS_HS_CLIENT_HELLO         1
#define TLS_HS_SERVER_HELLO         2
#define TLS_HS_CERTIFICATE          11
#define TLS_HS_SERVER_KEY_EXCHANGE  12
#define TLS_HS_CERTIFICATE_REQUEST  13
#define TLS_HS_SERVER_HELLO_DONE    14
#define TLS_HS_CERTIFICATE_VERIFY   15
#define TLS_HS_CLIENT_KEY_EXCHANGE  16
#define TLS_HS_FINISHED             20

// Cipher suites we support
#define TLS_RSA_WITH_AES_128_CBC_SHA256     0x003C
#define TLS_RSA_WITH_AES_128_GCM_SHA256     0x009C

// Alert levels
#define TLS_ALERT_WARNING 1
#define TLS_ALERT_FATAL   2

// Alert descriptions
#define TLS_ALERT_CLOSE_NOTIFY           0
#define TLS_ALERT_UNEXPECTED_MESSAGE     10
#define TLS_ALERT_BAD_RECORD_MAC         20
#define TLS_ALERT_HANDSHAKE_FAILURE      40
#define TLS_ALERT_CERTIFICATE_UNKNOWN    46

// TLS state
typedef enum {
    TLS_STATE_INIT,
    TLS_STATE_CLIENT_HELLO_SENT,
    TLS_STATE_SERVER_HELLO_RECEIVED,
    TLS_STATE_CERTIFICATE_RECEIVED,
    TLS_STATE_SERVER_HELLO_DONE_RECEIVED,
    TLS_STATE_CLIENT_KEY_EXCHANGE_SENT,
    TLS_STATE_CHANGE_CIPHER_SPEC_SENT,
    TLS_STATE_FINISHED_SENT,
    TLS_STATE_ESTABLISHED,
    TLS_STATE_ERROR
} tls_state_t;

// TLS connection context
typedef struct {
    tls_state_t state;
    int tcp_conn;           // Underlying TCP connection
    
    // Version negotiated
    uint16_t version;
    
    // Cipher suite
    uint16_t cipher_suite;
    
    // Random values
    uint8_t client_random[32];
    uint8_t server_random[32];
    
    // Session
    uint8_t session_id[32];
    uint8_t session_id_len;
    
    // Pre-master secret (48 bytes)
    uint8_t pre_master_secret[48];
    
    // Master secret (48 bytes)
    uint8_t master_secret[48];
    
    // Key material
    uint8_t client_write_MAC_key[32];
    uint8_t server_write_MAC_key[32];
    uint8_t client_write_key[16];
    uint8_t server_write_key[16];
    uint8_t client_write_IV[16];
    uint8_t server_write_IV[16];
    
    // Encryption contexts
    aes_ctx_t client_aes;
    aes_ctx_t server_aes;
    
    // GCM contexts (if using GCM)
    aes_gcm_ctx_t client_gcm;
    aes_gcm_ctx_t server_gcm;
    
    // Sequence numbers
    uint64_t client_seq;
    uint64_t server_seq;
    
    // Handshake hash
    sha256_ctx_t handshake_hash;
    
    // Server's public key (from certificate)
    rsa_public_key_t server_key;
    
    // Error info
    uint8_t alert_level;
    uint8_t alert_desc;
} tls_conn_t;

// TLS record header
typedef struct __attribute__((packed)) {
    uint8_t content_type;
    uint16_t version;
    uint16_t length;
} tls_record_t;

// Initialize TLS connection
void tls_init(tls_conn_t* conn, int tcp_conn);

// Perform TLS handshake
// Returns 0 on success, -1 on error
int tls_handshake(tls_conn_t* conn, const char* hostname);

// Send encrypted data
int tls_send(tls_conn_t* conn, const uint8_t* data, size_t len);

// Receive and decrypt data
int tls_recv(tls_conn_t* conn, uint8_t* data, size_t max_len);

// Close TLS connection
void tls_close(tls_conn_t* conn);

// Check if connected
int tls_is_connected(tls_conn_t* conn);

// Get error description
const char* tls_get_error(tls_conn_t* conn);

#endif // TLS_H

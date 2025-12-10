//
// TLS 1.2 Implementation
// Supports RSA key exchange with AES-128-CBC-SHA256 or AES-128-GCM-SHA256
//

#include "tls.h"
#include "tcp.h"
#include "e1000.h"
#include "tty.h"

// PRF (Pseudo-Random Function) for TLS 1.2 using HMAC-SHA256
static void tls_prf_sha256(const uint8_t* secret, size_t secret_len,
                           const char* label, 
                           const uint8_t* seed, size_t seed_len,
                           uint8_t* output, size_t output_len) {
    // A(0) = seed
    // A(i) = HMAC(secret, A(i-1))
    // PRF = HMAC(secret, A(1) || label || seed) || HMAC(secret, A(2) || label || seed) || ...
    
    size_t label_len = 0;
    while (label[label_len]) label_len++;
    
    // Combined seed = label || seed
    uint8_t combined[256];
    size_t combined_len = 0;
    for (size_t i = 0; i < label_len && combined_len < sizeof(combined); i++) {
        combined[combined_len++] = label[i];
    }
    for (size_t i = 0; i < seed_len && combined_len < sizeof(combined); i++) {
        combined[combined_len++] = seed[i];
    }
    
    uint8_t A[32];  // A(i)
    
    // A(1) = HMAC(secret, seed)
    hmac_sha256(secret, secret_len, combined, combined_len, A);
    
    size_t generated = 0;
    while (generated < output_len) {
        // P_hash = HMAC(secret, A(i) || seed)
        uint8_t p_input[256];
        size_t p_len = 0;
        for (int i = 0; i < 32; i++) p_input[p_len++] = A[i];
        for (size_t i = 0; i < combined_len && p_len < sizeof(p_input); i++) {
            p_input[p_len++] = combined[i];
        }
        
        uint8_t p_hash[32];
        hmac_sha256(secret, secret_len, p_input, p_len, p_hash);
        
        for (int i = 0; i < 32 && generated < output_len; i++) {
            output[generated++] = p_hash[i];
        }
        
        // A(i+1) = HMAC(secret, A(i))
        hmac_sha256(secret, secret_len, A, 32, A);
    }
}

// Generate random bytes (NOT secure - for demo only!)
static void generate_random(uint8_t* buf, size_t len) {
    static uint32_t seed = 0x12345678;
    for (size_t i = 0; i < len; i++) {
        seed = seed * 1103515245 + 12345;
        buf[i] = (seed >> 16) & 0xff;
    }
}

// Swap bytes (network order)
static uint16_t swap16(uint16_t x) {
    return (x >> 8) | (x << 8);
}

static uint32_t swap32(uint32_t x) {
    return ((x >> 24) & 0xff) | ((x >> 8) & 0xff00) | 
           ((x << 8) & 0xff0000) | ((x << 24) & 0xff000000);
}

void tls_init(tls_conn_t* conn, int tcp_conn) {
    // Zero everything
    uint8_t* p = (uint8_t*)conn;
    for (size_t i = 0; i < sizeof(tls_conn_t); i++) p[i] = 0;
    
    conn->tcp_conn = tcp_conn;
    conn->state = TLS_STATE_INIT;
    conn->version = TLS_VERSION_1_2;
    
    // Initialize handshake hash
    sha256_init(&conn->handshake_hash);
}

// Send raw TLS record
static int tls_send_record(tls_conn_t* conn, uint8_t content_type, 
                           const uint8_t* data, size_t len) {
    uint8_t record[5 + 16384];  // Max TLS record size
    
    record[0] = content_type;
    record[1] = (conn->version >> 8) & 0xff;
    record[2] = conn->version & 0xff;
    record[3] = (len >> 8) & 0xff;
    record[4] = len & 0xff;
    
    for (size_t i = 0; i < len; i++) {
        record[5 + i] = data[i];
    }
    
    return tcp_send(conn->tcp_conn, (char*)record, 5 + len);
}

// Receive TLS record
static int tls_recv_record(tls_conn_t* conn, uint8_t* content_type,
                           uint8_t* data, size_t max_len) {
    uint8_t header[5];
    int received = 0;
    
    // Wait for header with polling
    for (volatile int timeout = 0; timeout < 50000000 && received < 5; timeout++) {
        e1000_poll();
        int r = tcp_recv(conn->tcp_conn, (char*)(header + received), 5 - received);
        if (r > 0) {
            received += r;
            timeout = 0;
        }
    }
    
    if (received < 5) return -1;
    
    *content_type = header[0];
    uint16_t length = ((uint16_t)header[3] << 8) | header[4];
    
    if (length > max_len) return -1;
    
    // Receive payload
    received = 0;
    for (volatile int timeout = 0; timeout < 50000000 && received < (int)length; timeout++) {
        e1000_poll();
        int r = tcp_recv(conn->tcp_conn, (char*)(data + received), length - received);
        if (r > 0) {
            received += r;
            timeout = 0;
        }
    }
    
    return received;
}

// Build and send ClientHello
static int tls_send_client_hello(tls_conn_t* conn, const char* hostname) {
    uint8_t msg[512];
    int pos = 0;
    
    // Handshake type: ClientHello
    msg[pos++] = TLS_HS_CLIENT_HELLO;
    
    // Length placeholder (3 bytes)
    int len_pos = pos;
    pos += 3;
    
    // Client version (TLS 1.2)
    msg[pos++] = 0x03;
    msg[pos++] = 0x03;
    
    // Client random (32 bytes)
    // First 4 bytes: Unix time (we'll use fake time)
    msg[pos++] = 0x5f;
    msg[pos++] = 0x5f;
    msg[pos++] = 0x5f;
    msg[pos++] = 0x5f;
    
    // Remaining 28 bytes: random
    generate_random(&msg[pos], 28);
    for (int i = 0; i < 32; i++) conn->client_random[i] = msg[pos - 4 + i];
    pos += 28;
    
    // Session ID length (0 = new session)
    msg[pos++] = 0;
    
    // Cipher suites
    msg[pos++] = 0x00;  // Length high byte
    msg[pos++] = 0x04;  // Length low byte (2 cipher suites * 2 bytes)
    msg[pos++] = (TLS_RSA_WITH_AES_128_GCM_SHA256 >> 8) & 0xff;
    msg[pos++] = TLS_RSA_WITH_AES_128_GCM_SHA256 & 0xff;
    msg[pos++] = (TLS_RSA_WITH_AES_128_CBC_SHA256 >> 8) & 0xff;
    msg[pos++] = TLS_RSA_WITH_AES_128_CBC_SHA256 & 0xff;
    
    // Compression methods
    msg[pos++] = 0x01;  // Length
    msg[pos++] = 0x00;  // null compression
    
    // Extensions
    int ext_len_pos = pos;
    pos += 2;  // Extension length placeholder
    int ext_start = pos;
    
    // SNI extension (Server Name Indication)
    if (hostname) {
        size_t hostname_len = 0;
        while (hostname[hostname_len]) hostname_len++;
        
        msg[pos++] = 0x00;  // Extension type: SNI
        msg[pos++] = 0x00;
        
        uint16_t sni_len = hostname_len + 5;
        msg[pos++] = (sni_len >> 8) & 0xff;
        msg[pos++] = sni_len & 0xff;
        
        msg[pos++] = ((hostname_len + 3) >> 8) & 0xff;
        msg[pos++] = (hostname_len + 3) & 0xff;
        
        msg[pos++] = 0x00;  // Host name type
        msg[pos++] = (hostname_len >> 8) & 0xff;
        msg[pos++] = hostname_len & 0xff;
        
        for (size_t i = 0; i < hostname_len; i++) {
            msg[pos++] = hostname[i];
        }
    }
    
    // Signature algorithms extension
    msg[pos++] = 0x00;  // Extension type: signature_algorithms
    msg[pos++] = 0x0d;
    msg[pos++] = 0x00;  // Length
    msg[pos++] = 0x04;
    msg[pos++] = 0x00;  // Algorithms length
    msg[pos++] = 0x02;
    msg[pos++] = 0x04;  // SHA256
    msg[pos++] = 0x01;  // RSA
    
    // Fill in extension length
    uint16_t ext_len = pos - ext_start;
    msg[ext_len_pos] = (ext_len >> 8) & 0xff;
    msg[ext_len_pos + 1] = ext_len & 0xff;
    
    // Fill in handshake length
    uint32_t hs_len = pos - len_pos - 3;
    msg[len_pos] = (hs_len >> 16) & 0xff;
    msg[len_pos + 1] = (hs_len >> 8) & 0xff;
    msg[len_pos + 2] = hs_len & 0xff;
    
    // Update handshake hash
    sha256_update(&conn->handshake_hash, msg, pos);
    
    // Send as handshake record
    if (tls_send_record(conn, TLS_CONTENT_HANDSHAKE, msg, pos) < 0) {
        return -1;
    }
    
    conn->state = TLS_STATE_CLIENT_HELLO_SENT;
    return 0;
}

// Parse ServerHello
static int tls_parse_server_hello(tls_conn_t* conn, const uint8_t* data, size_t len) {
    if (len < 38) return -1;
    
    // Version
    conn->version = ((uint16_t)data[0] << 8) | data[1];
    if (conn->version != TLS_VERSION_1_2) {
        tty_putstr("TLS: Unsupported version\n");
        return -1;
    }
    
    // Server random
    for (int i = 0; i < 32; i++) {
        conn->server_random[i] = data[2 + i];
    }
    
    // Session ID
    conn->session_id_len = data[34];
    if (conn->session_id_len > 32) return -1;
    for (int i = 0; i < conn->session_id_len; i++) {
        conn->session_id[i] = data[35 + i];
    }
    
    int pos = 35 + conn->session_id_len;
    
    // Cipher suite
    conn->cipher_suite = ((uint16_t)data[pos] << 8) | data[pos + 1];
    pos += 2;
    
    tty_putstr("TLS: Using cipher suite 0x");
    // Simple hex print
    char hex[5];
    hex[0] = "0123456789ABCDEF"[(conn->cipher_suite >> 12) & 0xf];
    hex[1] = "0123456789ABCDEF"[(conn->cipher_suite >> 8) & 0xf];
    hex[2] = "0123456789ABCDEF"[(conn->cipher_suite >> 4) & 0xf];
    hex[3] = "0123456789ABCDEF"[conn->cipher_suite & 0xf];
    hex[4] = 0;
    tty_putstr(hex);
    tty_putstr("\n");
    
    conn->state = TLS_STATE_SERVER_HELLO_RECEIVED;
    return 0;
}

// Parse Certificate
static int tls_parse_certificate(tls_conn_t* conn, const uint8_t* data, size_t len) {
    if (len < 3) return -1;
    
    // Total certificates length
    uint32_t total_len = ((uint32_t)data[0] << 16) | ((uint32_t)data[1] << 8) | data[2];
    if (total_len + 3 > len) return -1;
    
    // Parse first certificate (we don't validate chain)
    int pos = 3;
    if (pos + 3 > (int)len) return -1;
    
    uint32_t cert_len = ((uint32_t)data[pos] << 16) | ((uint32_t)data[pos+1] << 8) | data[pos+2];
    pos += 3;
    
    if (pos + cert_len > len) return -1;
    
    // Parse X.509 certificate to extract RSA public key
    // This is a simplified parser - real implementation would be more robust
    const uint8_t* cert = &data[pos];
    
    // Skip outer SEQUENCE
    int cert_pos = 0;
    if (cert[cert_pos++] != 0x30) return -1;
    
    // Skip length
    if (cert[cert_pos] & 0x80) {
        int len_bytes = cert[cert_pos] & 0x7f;
        cert_pos += 1 + len_bytes;
    } else {
        cert_pos++;
    }
    
    // We need to find the SubjectPublicKeyInfo
    // This is a very simplified search - look for RSA OID followed by BIT STRING
    
    // RSA OID: 1.2.840.113549.1.1.1 = 06 09 2A 86 48 86 F7 0D 01 01 01
    const uint8_t rsa_oid[] = {0x06, 0x09, 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x01};
    
    int found = -1;
    for (size_t i = 0; i < cert_len - sizeof(rsa_oid); i++) {
        int match = 1;
        for (size_t j = 0; j < sizeof(rsa_oid); j++) {
            if (cert[i + j] != rsa_oid[j]) {
                match = 0;
                break;
            }
        }
        if (match) {
            found = i;
            break;
        }
    }
    
    if (found < 0) {
        tty_putstr("TLS: Could not find RSA key in certificate\n");
        return -1;
    }
    
    // Skip to BIT STRING containing the key
    int key_pos = found + sizeof(rsa_oid);
    while (key_pos < (int)cert_len && cert[key_pos] != 0x03) key_pos++;
    
    if (key_pos >= (int)cert_len) return -1;
    
    // Parse BIT STRING
    key_pos++;  // Skip 0x03
    
    // Get length
    int key_len;
    if (cert[key_pos] & 0x80) {
        int len_bytes = cert[key_pos] & 0x7f;
        key_len = 0;
        key_pos++;
        for (int i = 0; i < len_bytes; i++) {
            key_len = (key_len << 8) | cert[key_pos++];
        }
    } else {
        key_len = cert[key_pos++];
    }
    
    // Skip unused bits byte
    key_pos++;
    key_len--;
    
    // Now we have the RSA public key as DER-encoded SEQUENCE
    // Parse: SEQUENCE { INTEGER (n), INTEGER (e) }
    if (cert[key_pos] != 0x30) return -1;
    key_pos++;
    
    // Skip sequence length
    if (cert[key_pos] & 0x80) {
        int len_bytes = cert[key_pos] & 0x7f;
        key_pos += 1 + len_bytes;
    } else {
        key_pos++;
    }
    
    // Parse modulus (n)
    if (cert[key_pos] != 0x02) return -1;
    key_pos++;
    
    int n_len;
    if (cert[key_pos] & 0x80) {
        int len_bytes = cert[key_pos] & 0x7f;
        n_len = 0;
        key_pos++;
        for (int i = 0; i < len_bytes; i++) {
            n_len = (n_len << 8) | cert[key_pos++];
        }
    } else {
        n_len = cert[key_pos++];
    }
    
    // Skip leading zero if present
    const uint8_t* n_data = &cert[key_pos];
    if (n_data[0] == 0x00 && n_len > 1) {
        n_data++;
        n_len--;
    }
    
    key_pos += n_len + (n_data - &cert[key_pos - n_len]);
    
    // Parse exponent (e)
    if (cert[key_pos] != 0x02) return -1;
    key_pos++;
    
    int e_len;
    if (cert[key_pos] & 0x80) {
        int len_bytes = cert[key_pos] & 0x7f;
        e_len = 0;
        key_pos++;
        for (int i = 0; i < len_bytes; i++) {
            e_len = (e_len << 8) | cert[key_pos++];
        }
    } else {
        e_len = cert[key_pos++];
    }
    
    const uint8_t* e_data = &cert[key_pos];
    
    // Initialize RSA key
    rsa_init_public_key(&conn->server_key, n_data, n_len, e_data, e_len);
    
    tty_putstr("TLS: Got ");
    tty_putdec(conn->server_key.bits);
    tty_putstr("-bit RSA key\n");
    
    conn->state = TLS_STATE_CERTIFICATE_RECEIVED;
    return 0;
}

// Send ClientKeyExchange
static int tls_send_client_key_exchange(tls_conn_t* conn) {
    // Generate pre-master secret
    // First 2 bytes: client version
    conn->pre_master_secret[0] = 0x03;
    conn->pre_master_secret[1] = 0x03;
    // Remaining 46 bytes: random
    generate_random(&conn->pre_master_secret[2], 46);
    
    // Encrypt with server's RSA public key
    uint8_t encrypted[512];
    int enc_len = rsa_encrypt(&conn->server_key, conn->pre_master_secret, 48,
                              encrypted, sizeof(encrypted));
    
    if (enc_len < 0) {
        tty_putstr("TLS: RSA encryption failed\n");
        return -1;
    }
    
    // Build ClientKeyExchange message
    uint8_t msg[520];
    int pos = 0;
    
    msg[pos++] = TLS_HS_CLIENT_KEY_EXCHANGE;
    
    // Length (3 bytes)
    uint32_t hs_len = enc_len + 2;
    msg[pos++] = (hs_len >> 16) & 0xff;
    msg[pos++] = (hs_len >> 8) & 0xff;
    msg[pos++] = hs_len & 0xff;
    
    // Encrypted pre-master secret length
    msg[pos++] = (enc_len >> 8) & 0xff;
    msg[pos++] = enc_len & 0xff;
    
    for (int i = 0; i < enc_len; i++) {
        msg[pos++] = encrypted[i];
    }
    
    // Update handshake hash
    sha256_update(&conn->handshake_hash, msg, pos);
    
    if (tls_send_record(conn, TLS_CONTENT_HANDSHAKE, msg, pos) < 0) {
        return -1;
    }
    
    conn->state = TLS_STATE_CLIENT_KEY_EXCHANGE_SENT;
    return 0;
}

// Derive keys from master secret
static void tls_derive_keys(tls_conn_t* conn) {
    // Compute master secret
    // master_secret = PRF(pre_master_secret, "master secret", client_random + server_random)
    uint8_t seed[64];
    for (int i = 0; i < 32; i++) {
        seed[i] = conn->client_random[i];
        seed[32 + i] = conn->server_random[i];
    }
    
    tls_prf_sha256(conn->pre_master_secret, 48, "master secret",
                   seed, 64, conn->master_secret, 48);
    
    // Compute key block
    // key_block = PRF(master_secret, "key expansion", server_random + client_random)
    for (int i = 0; i < 32; i++) {
        seed[i] = conn->server_random[i];
        seed[32 + i] = conn->client_random[i];
    }
    
    // Key block size depends on cipher suite
    // AES-128-CBC-SHA256: 32 + 32 + 16 + 16 + 16 + 16 = 128 bytes
    // AES-128-GCM-SHA256: 16 + 16 + 4 + 4 = 40 bytes (no MAC keys)
    uint8_t key_block[128];
    
    if (conn->cipher_suite == TLS_RSA_WITH_AES_128_GCM_SHA256) {
        tls_prf_sha256(conn->master_secret, 48, "key expansion",
                       seed, 64, key_block, 40);
        
        // client_write_key
        for (int i = 0; i < 16; i++) conn->client_write_key[i] = key_block[i];
        // server_write_key
        for (int i = 0; i < 16; i++) conn->server_write_key[i] = key_block[16 + i];
        // client_write_IV (implicit IV, 4 bytes)
        for (int i = 0; i < 4; i++) conn->client_write_IV[i] = key_block[32 + i];
        // server_write_IV
        for (int i = 0; i < 4; i++) conn->server_write_IV[i] = key_block[36 + i];
    } else {
        tls_prf_sha256(conn->master_secret, 48, "key expansion",
                       seed, 64, key_block, 128);
        
        int pos = 0;
        for (int i = 0; i < 32; i++) conn->client_write_MAC_key[i] = key_block[pos++];
        for (int i = 0; i < 32; i++) conn->server_write_MAC_key[i] = key_block[pos++];
        for (int i = 0; i < 16; i++) conn->client_write_key[i] = key_block[pos++];
        for (int i = 0; i < 16; i++) conn->server_write_key[i] = key_block[pos++];
        for (int i = 0; i < 16; i++) conn->client_write_IV[i] = key_block[pos++];
        for (int i = 0; i < 16; i++) conn->server_write_IV[i] = key_block[pos++];
    }
    
    // Initialize AES contexts
    aes_init(&conn->client_aes, conn->client_write_key);
    aes_init(&conn->server_aes, conn->server_write_key);
}

// Send ChangeCipherSpec
static int tls_send_change_cipher_spec(tls_conn_t* conn) {
    uint8_t msg[1] = {0x01};
    
    if (tls_send_record(conn, TLS_CONTENT_CHANGE_CIPHER_SPEC, msg, 1) < 0) {
        return -1;
    }
    
    conn->state = TLS_STATE_CHANGE_CIPHER_SPEC_SENT;
    return 0;
}

// Send encrypted Finished
static int tls_send_finished(tls_conn_t* conn) {
    // Compute verify_data = PRF(master_secret, "client finished", Hash(handshake_messages))
    uint8_t handshake_hash[32];
    sha256_ctx_t hash_copy = conn->handshake_hash;
    sha256_final(&hash_copy, handshake_hash);
    
    uint8_t verify_data[12];
    tls_prf_sha256(conn->master_secret, 48, "client finished",
                   handshake_hash, 32, verify_data, 12);
    
    // Build Finished message
    uint8_t finished[16];
    finished[0] = TLS_HS_FINISHED;
    finished[1] = 0x00;
    finished[2] = 0x00;
    finished[3] = 0x0c;  // 12 bytes verify_data
    for (int i = 0; i < 12; i++) {
        finished[4 + i] = verify_data[i];
    }
    
    // Encrypt the Finished message
    if (conn->cipher_suite == TLS_RSA_WITH_AES_128_GCM_SHA256) {
        // GCM mode
        uint8_t nonce[12];
        for (int i = 0; i < 4; i++) nonce[i] = conn->client_write_IV[i];
        // Explicit nonce (8 bytes from sequence number)
        for (int i = 0; i < 8; i++) {
            nonce[4 + i] = (conn->client_seq >> ((7 - i) * 8)) & 0xff;
        }
        
        aes_gcm_ctx_t gcm;
        aes_gcm_init(&gcm, conn->client_write_key, nonce, 12);
        
        // Additional authenticated data: seq_num || type || version || length
        uint8_t aad[13];
        for (int i = 0; i < 8; i++) {
            aad[i] = (conn->client_seq >> ((7 - i) * 8)) & 0xff;
        }
        aad[8] = TLS_CONTENT_HANDSHAKE;
        aad[9] = 0x03;
        aad[10] = 0x03;
        aad[11] = 0x00;
        aad[12] = 16;  // Finished length
        
        uint8_t tag[16];
        aes_gcm_encrypt(&gcm, aad, 13, finished, 16, tag);
        
        // Send: explicit_nonce || ciphertext || tag
        uint8_t encrypted[8 + 16 + 16];
        for (int i = 0; i < 8; i++) encrypted[i] = nonce[4 + i];
        for (int i = 0; i < 16; i++) encrypted[8 + i] = finished[i];
        for (int i = 0; i < 16; i++) encrypted[24 + i] = tag[i];
        
        if (tls_send_record(conn, TLS_CONTENT_HANDSHAKE, encrypted, 40) < 0) {
            return -1;
        }
    } else {
        // CBC mode with HMAC
        // MAC = HMAC(MAC_key, seq_num || type || version || length || content)
        uint8_t mac_input[13 + 16];
        for (int i = 0; i < 8; i++) {
            mac_input[i] = (conn->client_seq >> ((7 - i) * 8)) & 0xff;
        }
        mac_input[8] = TLS_CONTENT_HANDSHAKE;
        mac_input[9] = 0x03;
        mac_input[10] = 0x03;
        mac_input[11] = 0x00;
        mac_input[12] = 16;
        for (int i = 0; i < 16; i++) mac_input[13 + i] = finished[i];
        
        uint8_t mac[32];
        hmac_sha256(conn->client_write_MAC_key, 32, mac_input, 29, mac);
        
        // Pad to block size
        uint8_t plaintext[64];
        int plain_len = 0;
        for (int i = 0; i < 16; i++) plaintext[plain_len++] = finished[i];
        for (int i = 0; i < 32; i++) plaintext[plain_len++] = mac[i];
        
        // PKCS#7 padding
        int pad_len = 16 - (plain_len % 16);
        for (int i = 0; i < pad_len; i++) {
            plaintext[plain_len++] = pad_len - 1;
        }
        
        // Encrypt
        uint8_t iv[16];
        generate_random(iv, 16);
        
        aes_cbc_encrypt(&conn->client_aes, iv, plaintext, plain_len);
        
        // Send: IV || ciphertext
        uint8_t encrypted[16 + 64];
        for (int i = 0; i < 16; i++) encrypted[i] = iv[i];
        for (int i = 0; i < plain_len; i++) encrypted[16 + i] = plaintext[i];
        
        if (tls_send_record(conn, TLS_CONTENT_HANDSHAKE, encrypted, 16 + plain_len) < 0) {
            return -1;
        }
    }
    
    conn->client_seq++;
    conn->state = TLS_STATE_FINISHED_SENT;
    return 0;
}

int tls_handshake(tls_conn_t* conn, const char* hostname) {
    uint8_t buffer[4096];
    uint8_t content_type;
    int len;
    
    tty_putstr("TLS: Starting handshake...\n");
    
    // Send ClientHello
    if (tls_send_client_hello(conn, hostname) < 0) {
        tty_putstr("TLS: Failed to send ClientHello\n");
        return -1;
    }
    
    tty_putstr("TLS: ClientHello sent\n");
    
    // Receive ServerHello, Certificate, ServerHelloDone
    int got_server_hello = 0;
    int got_certificate = 0;
    int got_server_hello_done = 0;
    
    while (!got_server_hello_done) {
        len = tls_recv_record(conn, &content_type, buffer, sizeof(buffer));
        if (len < 0) {
            tty_putstr("TLS: Failed to receive record\n");
            return -1;
        }
        
        if (content_type == TLS_CONTENT_ALERT) {
            tty_putstr("TLS: Received alert\n");
            return -1;
        }
        
        if (content_type != TLS_CONTENT_HANDSHAKE) {
            continue;
        }
        
        // Update handshake hash
        sha256_update(&conn->handshake_hash, buffer, len);
        
        // Parse handshake messages
        int pos = 0;
        while (pos < len) {
            uint8_t hs_type = buffer[pos];
            uint32_t hs_len = ((uint32_t)buffer[pos+1] << 16) | 
                              ((uint32_t)buffer[pos+2] << 8) | 
                              buffer[pos+3];
            pos += 4;
            
            switch (hs_type) {
                case TLS_HS_SERVER_HELLO:
                    if (tls_parse_server_hello(conn, &buffer[pos], hs_len) < 0) {
                        return -1;
                    }
                    got_server_hello = 1;
                    tty_putstr("TLS: ServerHello received\n");
                    break;
                    
                case TLS_HS_CERTIFICATE:
                    if (tls_parse_certificate(conn, &buffer[pos], hs_len) < 0) {
                        return -1;
                    }
                    got_certificate = 1;
                    tty_putstr("TLS: Certificate received\n");
                    break;
                    
                case TLS_HS_SERVER_HELLO_DONE:
                    got_server_hello_done = 1;
                    tty_putstr("TLS: ServerHelloDone received\n");
                    break;
            }
            
            pos += hs_len;
        }
    }
    
    conn->state = TLS_STATE_SERVER_HELLO_DONE_RECEIVED;
    
    // Send ClientKeyExchange
    if (tls_send_client_key_exchange(conn) < 0) {
        tty_putstr("TLS: Failed to send ClientKeyExchange\n");
        return -1;
    }
    tty_putstr("TLS: ClientKeyExchange sent\n");
    
    // Derive keys
    tls_derive_keys(conn);
    tty_putstr("TLS: Keys derived\n");
    
    // Send ChangeCipherSpec
    if (tls_send_change_cipher_spec(conn) < 0) {
        return -1;
    }
    tty_putstr("TLS: ChangeCipherSpec sent\n");
    
    // Send Finished
    if (tls_send_finished(conn) < 0) {
        return -1;
    }
    tty_putstr("TLS: Finished sent\n");
    
    // Receive server's ChangeCipherSpec and Finished
    int got_server_ccs = 0;
    int got_server_finished = 0;
    
    while (!got_server_finished) {
        len = tls_recv_record(conn, &content_type, buffer, sizeof(buffer));
        if (len < 0) {
            tty_putstr("TLS: Failed to receive server response\n");
            return -1;
        }
        
        if (content_type == TLS_CONTENT_CHANGE_CIPHER_SPEC) {
            got_server_ccs = 1;
            tty_putstr("TLS: Server ChangeCipherSpec received\n");
        } else if (content_type == TLS_CONTENT_HANDSHAKE && got_server_ccs) {
            // This should be the encrypted Finished
            // We'll just trust it for now (proper implementation would verify)
            got_server_finished = 1;
            tty_putstr("TLS: Server Finished received\n");
        } else if (content_type == TLS_CONTENT_ALERT) {
            tty_putstr("TLS: Received alert from server\n");
            return -1;
        }
    }
    
    conn->state = TLS_STATE_ESTABLISHED;
    tty_putstr("TLS: Handshake complete!\n");
    
    return 0;
}

int tls_send(tls_conn_t* conn, const uint8_t* data, size_t len) {
    if (conn->state != TLS_STATE_ESTABLISHED) {
        return -1;
    }
    
    if (conn->cipher_suite == TLS_RSA_WITH_AES_128_GCM_SHA256) {
        // GCM encryption
        uint8_t nonce[12];
        for (int i = 0; i < 4; i++) nonce[i] = conn->client_write_IV[i];
        for (int i = 0; i < 8; i++) {
            nonce[4 + i] = (conn->client_seq >> ((7 - i) * 8)) & 0xff;
        }
        
        aes_gcm_ctx_t gcm;
        aes_gcm_init(&gcm, conn->client_write_key, nonce, 12);
        
        uint8_t aad[13];
        for (int i = 0; i < 8; i++) {
            aad[i] = (conn->client_seq >> ((7 - i) * 8)) & 0xff;
        }
        aad[8] = TLS_CONTENT_APPLICATION_DATA;
        aad[9] = 0x03;
        aad[10] = 0x03;
        aad[11] = (len >> 8) & 0xff;
        aad[12] = len & 0xff;
        
        uint8_t* encrypted = (uint8_t*)data;  // Warning: modifies input!
        uint8_t ciphertext[16384];
        for (size_t i = 0; i < len; i++) ciphertext[i] = data[i];
        
        uint8_t tag[16];
        aes_gcm_encrypt(&gcm, aad, 13, ciphertext, len, tag);
        
        uint8_t record[8 + 16384 + 16];
        for (int i = 0; i < 8; i++) record[i] = nonce[4 + i];
        for (size_t i = 0; i < len; i++) record[8 + i] = ciphertext[i];
        for (int i = 0; i < 16; i++) record[8 + len + i] = tag[i];
        
        int ret = tls_send_record(conn, TLS_CONTENT_APPLICATION_DATA, record, 8 + len + 16);
        conn->client_seq++;
        return ret;
    } else {
        // CBC mode
        uint8_t mac_input[13 + 16384];
        for (int i = 0; i < 8; i++) {
            mac_input[i] = (conn->client_seq >> ((7 - i) * 8)) & 0xff;
        }
        mac_input[8] = TLS_CONTENT_APPLICATION_DATA;
        mac_input[9] = 0x03;
        mac_input[10] = 0x03;
        mac_input[11] = (len >> 8) & 0xff;
        mac_input[12] = len & 0xff;
        for (size_t i = 0; i < len; i++) mac_input[13 + i] = data[i];
        
        uint8_t mac[32];
        hmac_sha256(conn->client_write_MAC_key, 32, mac_input, 13 + len, mac);
        
        // Build plaintext: data || MAC || padding
        uint8_t plaintext[16384 + 32 + 16];
        size_t plain_len = 0;
        for (size_t i = 0; i < len; i++) plaintext[plain_len++] = data[i];
        for (int i = 0; i < 32; i++) plaintext[plain_len++] = mac[i];
        
        int pad_len = 16 - (plain_len % 16);
        for (int i = 0; i < pad_len; i++) {
            plaintext[plain_len++] = pad_len - 1;
        }
        
        uint8_t iv[16];
        generate_random(iv, 16);
        
        aes_cbc_encrypt(&conn->client_aes, iv, plaintext, plain_len);
        
        uint8_t record[16 + 16384 + 32 + 16];
        for (int i = 0; i < 16; i++) record[i] = iv[i];
        for (size_t i = 0; i < plain_len; i++) record[16 + i] = plaintext[i];
        
        int ret = tls_send_record(conn, TLS_CONTENT_APPLICATION_DATA, record, 16 + plain_len);
        conn->client_seq++;
        return ret;
    }
}

int tls_recv(tls_conn_t* conn, uint8_t* data, size_t max_len) {
    if (conn->state != TLS_STATE_ESTABLISHED) {
        return -1;
    }
    
    uint8_t buffer[16384 + 256];
    uint8_t content_type;
    
    int len = tls_recv_record(conn, &content_type, buffer, sizeof(buffer));
    if (len < 0) return -1;
    
    if (content_type == TLS_CONTENT_ALERT) {
        return -1;
    }
    
    if (content_type != TLS_CONTENT_APPLICATION_DATA) {
        return 0;
    }
    
    // Decrypt
    if (conn->cipher_suite == TLS_RSA_WITH_AES_128_GCM_SHA256) {
        if (len < 8 + 16) return -1;
        
        uint8_t nonce[12];
        for (int i = 0; i < 4; i++) nonce[i] = conn->server_write_IV[i];
        for (int i = 0; i < 8; i++) nonce[4 + i] = buffer[i];
        
        int ciphertext_len = len - 8 - 16;
        uint8_t* ciphertext = &buffer[8];
        uint8_t* tag = &buffer[8 + ciphertext_len];
        
        aes_gcm_ctx_t gcm;
        aes_gcm_init(&gcm, conn->server_write_key, nonce, 12);
        
        uint8_t aad[13];
        for (int i = 0; i < 8; i++) {
            aad[i] = (conn->server_seq >> ((7 - i) * 8)) & 0xff;
        }
        aad[8] = TLS_CONTENT_APPLICATION_DATA;
        aad[9] = 0x03;
        aad[10] = 0x03;
        aad[11] = (ciphertext_len >> 8) & 0xff;
        aad[12] = ciphertext_len & 0xff;
        
        if (aes_gcm_decrypt(&gcm, aad, 13, ciphertext, ciphertext_len, tag) != 0) {
            tty_putstr("TLS: GCM decryption failed\n");
            return -1;
        }
        
        conn->server_seq++;
        
        size_t copy_len = (ciphertext_len < (int)max_len) ? ciphertext_len : max_len;
        for (size_t i = 0; i < copy_len; i++) {
            data[i] = ciphertext[i];
        }
        return copy_len;
    } else {
        // CBC mode
        if (len < 16 + 32 + 1) return -1;
        
        uint8_t* iv = buffer;
        uint8_t* ciphertext = &buffer[16];
        int ciphertext_len = len - 16;
        
        aes_cbc_decrypt(&conn->server_aes, iv, ciphertext, ciphertext_len);
        
        // Remove padding
        uint8_t pad_val = ciphertext[ciphertext_len - 1];
        int plaintext_len = ciphertext_len - pad_val - 1 - 32;  // Remove padding and MAC
        
        if (plaintext_len < 0) return -1;
        
        // TODO: Verify MAC
        
        conn->server_seq++;
        
        size_t copy_len = (plaintext_len < (int)max_len) ? plaintext_len : max_len;
        for (size_t i = 0; i < copy_len; i++) {
            data[i] = ciphertext[i];
        }
        return copy_len;
    }
}

void tls_close(tls_conn_t* conn) {
    if (conn->state == TLS_STATE_ESTABLISHED) {
        // Send close_notify alert
        uint8_t alert[2] = {TLS_ALERT_WARNING, TLS_ALERT_CLOSE_NOTIFY};
        tls_send_record(conn, TLS_CONTENT_ALERT, alert, 2);
    }
    
    conn->state = TLS_STATE_INIT;
}

int tls_is_connected(tls_conn_t* conn) {
    return conn->state == TLS_STATE_ESTABLISHED;
}

const char* tls_get_error(tls_conn_t* conn) {
    switch (conn->alert_desc) {
        case TLS_ALERT_CLOSE_NOTIFY: return "Connection closed";
        case TLS_ALERT_UNEXPECTED_MESSAGE: return "Unexpected message";
        case TLS_ALERT_BAD_RECORD_MAC: return "Bad record MAC";
        case TLS_ALERT_HANDSHAKE_FAILURE: return "Handshake failure";
        default: return "Unknown error";
    }
}

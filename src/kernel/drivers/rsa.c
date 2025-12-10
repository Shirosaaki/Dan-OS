//
// RSA Implementation
//

#include "rsa.h"
#include "sha256.h"

// Simple random bytes (NOT cryptographically secure - for demo only!)
static uint8_t simple_random(void) {
    static uint32_t seed = 12345;
    seed = seed * 1103515245 + 12345;
    return (seed >> 16) & 0xff;
}

void rsa_init_public_key(rsa_public_key_t* key, 
                         const uint8_t* n, size_t n_len,
                         const uint8_t* e, size_t e_len) {
    bigint_from_bytes(&key->n, n, n_len);
    bigint_from_bytes(&key->e, e, e_len);
    key->bits = bigint_bits(&key->n);
}

// PKCS#1 v1.5 padding: 0x00 0x02 [random non-zero bytes] 0x00 [message]
int rsa_pkcs1_pad(uint8_t* padded, size_t padded_len,
                  const uint8_t* msg, size_t msg_len) {
    // Need at least 11 bytes overhead (0x00, 0x02, 8+ random bytes, 0x00)
    if (msg_len > padded_len - 11) {
        return -1;
    }
    
    size_t ps_len = padded_len - msg_len - 3;
    
    padded[0] = 0x00;
    padded[1] = 0x02;
    
    // Fill with non-zero random bytes
    for (size_t i = 0; i < ps_len; i++) {
        uint8_t r;
        do {
            r = simple_random();
        } while (r == 0);
        padded[2 + i] = r;
    }
    
    padded[2 + ps_len] = 0x00;
    
    // Copy message
    for (size_t i = 0; i < msg_len; i++) {
        padded[3 + ps_len + i] = msg[i];
    }
    
    return 0;
}

int rsa_encrypt(const rsa_public_key_t* key, 
                const uint8_t* in, size_t in_len,
                uint8_t* out, size_t out_size) {
    size_t key_bytes = (key->bits + 7) / 8;
    
    if (out_size < key_bytes) {
        return -1;
    }
    
    // PKCS#1 v1.5 pad the input
    uint8_t padded[512];  // Max 4096 bit key
    if (key_bytes > sizeof(padded)) {
        return -1;
    }
    
    if (rsa_pkcs1_pad(padded, key_bytes, in, in_len) != 0) {
        return -1;
    }
    
    // Convert to bigint
    bigint_t m;
    bigint_from_bytes(&m, padded, key_bytes);
    
    // c = m^e mod n
    bigint_t c;
    bigint_modpow(&c, &m, &key->e, &key->n);
    
    // Convert result to bytes
    bigint_to_bytes(&c, out, key_bytes);
    
    return key_bytes;
}

// DigestInfo for SHA-256 (DER encoded)
static const uint8_t sha256_digest_info[] = {
    0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86,
    0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01, 0x05,
    0x00, 0x04, 0x20
};

int rsa_pkcs1_verify(const rsa_public_key_t* key,
                     const uint8_t* hash, size_t hash_len,
                     const uint8_t* sig, size_t sig_len) {
    size_t key_bytes = (key->bits + 7) / 8;
    
    if (sig_len != key_bytes) {
        return -1;
    }
    
    // s^e mod n
    bigint_t s, m;
    bigint_from_bytes(&s, sig, sig_len);
    bigint_modpow(&m, &s, &key->e, &key->n);
    
    // Convert to bytes
    uint8_t decrypted[512];
    bigint_to_bytes(&m, decrypted, key_bytes);
    
    // Check PKCS#1 v1.5 signature padding
    // Format: 0x00 0x01 [0xff bytes] 0x00 [DigestInfo] [hash]
    if (decrypted[0] != 0x00 || decrypted[1] != 0x01) {
        return -1;
    }
    
    // Find 0x00 separator
    size_t sep_pos = 2;
    while (sep_pos < key_bytes && decrypted[sep_pos] == 0xff) {
        sep_pos++;
    }
    
    if (sep_pos >= key_bytes || decrypted[sep_pos] != 0x00) {
        return -1;
    }
    sep_pos++;
    
    // Check DigestInfo for SHA-256
    if (hash_len == 32) {
        if (key_bytes - sep_pos < sizeof(sha256_digest_info) + 32) {
            return -1;
        }
        
        for (size_t i = 0; i < sizeof(sha256_digest_info); i++) {
            if (decrypted[sep_pos + i] != sha256_digest_info[i]) {
                return -1;
            }
        }
        sep_pos += sizeof(sha256_digest_info);
    }
    
    // Compare hash
    for (size_t i = 0; i < hash_len; i++) {
        if (decrypted[sep_pos + i] != hash[i]) {
            return -1;
        }
    }
    
    return 0;
}

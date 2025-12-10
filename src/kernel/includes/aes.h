//
// AES-128 Encryption Header
//

#ifndef AES_H
#define AES_H

#include <stdint.h>
#include <stddef.h>

#define AES_BLOCK_SIZE 16
#define AES_128_KEY_SIZE 16
#define AES_128_ROUNDS 10

typedef struct {
    uint32_t round_keys[44];  // 11 round keys * 4 words
} aes_ctx_t;

// Initialize AES context with key
void aes_init(aes_ctx_t* ctx, const uint8_t* key);

// Encrypt single block (16 bytes)
void aes_encrypt_block(const aes_ctx_t* ctx, const uint8_t* in, uint8_t* out);

// Decrypt single block (16 bytes)
void aes_decrypt_block(const aes_ctx_t* ctx, const uint8_t* in, uint8_t* out);

// CBC mode encryption (in-place, data must be padded to block size)
void aes_cbc_encrypt(const aes_ctx_t* ctx, uint8_t* iv, uint8_t* data, size_t len);

// CBC mode decryption (in-place)
void aes_cbc_decrypt(const aes_ctx_t* ctx, uint8_t* iv, uint8_t* data, size_t len);

// GCM mode (for TLS 1.2)
typedef struct {
    aes_ctx_t aes;
    uint8_t H[16];      // Hash subkey
    uint8_t J0[16];     // Pre-counter block
    uint8_t counter[16];
} aes_gcm_ctx_t;

void aes_gcm_init(aes_gcm_ctx_t* ctx, const uint8_t* key, const uint8_t* iv, size_t iv_len);
void aes_gcm_encrypt(aes_gcm_ctx_t* ctx, const uint8_t* aad, size_t aad_len,
                     uint8_t* data, size_t data_len, uint8_t* tag);
int aes_gcm_decrypt(aes_gcm_ctx_t* ctx, const uint8_t* aad, size_t aad_len,
                    uint8_t* data, size_t data_len, const uint8_t* tag);

#endif // AES_H

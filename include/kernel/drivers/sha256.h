//
// SHA-256 Hash Implementation
//

#ifndef SHA256_H
#define SHA256_H

#include <stdint.h>
#include <stddef.h>

#define SHA256_BLOCK_SIZE 64
#define SHA256_DIGEST_SIZE 32

typedef struct {
    uint32_t state[8];
    uint64_t bitcount;
    uint8_t buffer[SHA256_BLOCK_SIZE];
} sha256_ctx_t;

// Initialize SHA-256 context
void sha256_init(sha256_ctx_t* ctx);

// Update hash with data
void sha256_update(sha256_ctx_t* ctx, const uint8_t* data, size_t len);

// Finalize and get hash
void sha256_final(sha256_ctx_t* ctx, uint8_t* hash);

// One-shot hash
void sha256(const uint8_t* data, size_t len, uint8_t* hash);

// HMAC-SHA256
void hmac_sha256(const uint8_t* key, size_t key_len,
                 const uint8_t* data, size_t data_len,
                 uint8_t* mac);

#endif // SHA256_H

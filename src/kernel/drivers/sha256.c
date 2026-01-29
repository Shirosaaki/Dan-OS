//
// SHA-256 Implementation
// Based on FIPS 180-4
//

#include <kernel/drivers/sha256.h>

// SHA-256 constants (first 32 bits of fractional parts of cube roots of first 64 primes)
static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

// Rotate right
#define ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

// SHA-256 functions
#define CH(x, y, z)  (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x)       (ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define EP1(x)       (ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25))
#define SIG0(x)      (ROTR(x, 7) ^ ROTR(x, 18) ^ ((x) >> 3))
#define SIG1(x)      (ROTR(x, 17) ^ ROTR(x, 19) ^ ((x) >> 10))

// Process one 64-byte block
static void sha256_transform(sha256_ctx_t* ctx, const uint8_t* data) {
    uint32_t a, b, c, d, e, f, g, h;
    uint32_t W[64];
    uint32_t t1, t2;
    int i;
    
    // Prepare message schedule
    for (i = 0; i < 16; i++) {
        W[i] = ((uint32_t)data[i * 4] << 24) |
               ((uint32_t)data[i * 4 + 1] << 16) |
               ((uint32_t)data[i * 4 + 2] << 8) |
               ((uint32_t)data[i * 4 + 3]);
    }
    
    for (i = 16; i < 64; i++) {
        W[i] = SIG1(W[i - 2]) + W[i - 7] + SIG0(W[i - 15]) + W[i - 16];
    }
    
    // Initialize working variables
    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];
    
    // Main loop
    for (i = 0; i < 64; i++) {
        t1 = h + EP1(e) + CH(e, f, g) + K[i] + W[i];
        t2 = EP0(a) + MAJ(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    
    // Update state
    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

void sha256_init(sha256_ctx_t* ctx) {
    // Initial hash values (first 32 bits of fractional parts of square roots of first 8 primes)
    ctx->state[0] = 0x6a09e667;
    ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372;
    ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f;
    ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab;
    ctx->state[7] = 0x5be0cd19;
    ctx->bitcount = 0;
}

void sha256_update(sha256_ctx_t* ctx, const uint8_t* data, size_t len) {
    size_t i;
    size_t index = (ctx->bitcount / 8) % SHA256_BLOCK_SIZE;
    
    ctx->bitcount += len * 8;
    
    for (i = 0; i < len; i++) {
        ctx->buffer[index++] = data[i];
        if (index == SHA256_BLOCK_SIZE) {
            sha256_transform(ctx, ctx->buffer);
            index = 0;
        }
    }
}

void sha256_final(sha256_ctx_t* ctx, uint8_t* hash) {
    size_t index = (ctx->bitcount / 8) % SHA256_BLOCK_SIZE;
    size_t pad_len;
    uint8_t pad[SHA256_BLOCK_SIZE * 2];
    int i;
    
    // Padding
    pad[0] = 0x80;
    for (i = 1; i < (int)sizeof(pad); i++) {
        pad[i] = 0;
    }
    
    if (index < 56) {
        pad_len = 56 - index;
    } else {
        pad_len = 120 - index;
    }
    
    sha256_update(ctx, pad, pad_len);
    
    // Append length in bits (big-endian)
    uint8_t len_bytes[8];
    len_bytes[0] = (ctx->bitcount >> 56) & 0xff;
    len_bytes[1] = (ctx->bitcount >> 48) & 0xff;
    len_bytes[2] = (ctx->bitcount >> 40) & 0xff;
    len_bytes[3] = (ctx->bitcount >> 32) & 0xff;
    len_bytes[4] = (ctx->bitcount >> 24) & 0xff;
    len_bytes[5] = (ctx->bitcount >> 16) & 0xff;
    len_bytes[6] = (ctx->bitcount >> 8) & 0xff;
    len_bytes[7] = ctx->bitcount & 0xff;
    
    // Fix: we need to add the length without updating bitcount again
    size_t buf_index = (ctx->bitcount / 8) % SHA256_BLOCK_SIZE;
    for (i = 0; i < 8; i++) {
        ctx->buffer[buf_index++] = len_bytes[i];
    }
    sha256_transform(ctx, ctx->buffer);
    
    // Output hash (big-endian)
    for (i = 0; i < 8; i++) {
        hash[i * 4] = (ctx->state[i] >> 24) & 0xff;
        hash[i * 4 + 1] = (ctx->state[i] >> 16) & 0xff;
        hash[i * 4 + 2] = (ctx->state[i] >> 8) & 0xff;
        hash[i * 4 + 3] = ctx->state[i] & 0xff;
    }
}

void sha256(const uint8_t* data, size_t len, uint8_t* hash) {
    sha256_ctx_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, hash);
}

// HMAC-SHA256
void hmac_sha256(const uint8_t* key, size_t key_len,
                 const uint8_t* data, size_t data_len,
                 uint8_t* mac) {
    sha256_ctx_t ctx;
    uint8_t k_ipad[SHA256_BLOCK_SIZE];
    uint8_t k_opad[SHA256_BLOCK_SIZE];
    uint8_t tk[SHA256_DIGEST_SIZE];
    uint8_t inner_hash[SHA256_DIGEST_SIZE];
    size_t i;
    
    // If key is longer than block size, hash it
    if (key_len > SHA256_BLOCK_SIZE) {
        sha256(key, key_len, tk);
        key = tk;
        key_len = SHA256_DIGEST_SIZE;
    }
    
    // Prepare pads
    for (i = 0; i < SHA256_BLOCK_SIZE; i++) {
        if (i < key_len) {
            k_ipad[i] = key[i] ^ 0x36;
            k_opad[i] = key[i] ^ 0x5c;
        } else {
            k_ipad[i] = 0x36;
            k_opad[i] = 0x5c;
        }
    }
    
    // Inner hash: H(K XOR ipad || data)
    sha256_init(&ctx);
    sha256_update(&ctx, k_ipad, SHA256_BLOCK_SIZE);
    sha256_update(&ctx, data, data_len);
    sha256_final(&ctx, inner_hash);
    
    // Outer hash: H(K XOR opad || inner_hash)
    sha256_init(&ctx);
    sha256_update(&ctx, k_opad, SHA256_BLOCK_SIZE);
    sha256_update(&ctx, inner_hash, SHA256_DIGEST_SIZE);
    sha256_final(&ctx, mac);
}

//
// AES-128 Implementation
// Based on FIPS 197
//

#include <kernel/drivers/aes.h>

// S-box
static const uint8_t sbox[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16
};

// Inverse S-box
static const uint8_t inv_sbox[256] = {
    0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38, 0xbf, 0x40, 0xa3, 0x9e, 0x81, 0xf3, 0xd7, 0xfb,
    0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87, 0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb,
    0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2, 0x23, 0x3d, 0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e,
    0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2, 0x76, 0x5b, 0xa2, 0x49, 0x6d, 0x8b, 0xd1, 0x25,
    0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16, 0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92,
    0x6c, 0x70, 0x48, 0x50, 0xfd, 0xed, 0xb9, 0xda, 0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84,
    0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a, 0xf7, 0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06,
    0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02, 0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b,
    0x3a, 0x91, 0x11, 0x41, 0x4f, 0x67, 0xdc, 0xea, 0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73,
    0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85, 0xe2, 0xf9, 0x37, 0xe8, 0x1c, 0x75, 0xdf, 0x6e,
    0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89, 0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b,
    0xfc, 0x56, 0x3e, 0x4b, 0xc6, 0xd2, 0x79, 0x20, 0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4,
    0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07, 0xc7, 0x31, 0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f,
    0x60, 0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d, 0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef,
    0xa0, 0xe0, 0x3b, 0x4d, 0xae, 0x2a, 0xf5, 0xb0, 0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61,
    0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26, 0xe1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0c, 0x7d
};

// Round constants
static const uint8_t rcon[11] = {
    0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36
};

// Galois field multiplication
static uint8_t gf_mul(uint8_t a, uint8_t b) {
    uint8_t p = 0;
    uint8_t hi_bit;
    for (int i = 0; i < 8; i++) {
        if (b & 1) p ^= a;
        hi_bit = a & 0x80;
        a <<= 1;
        if (hi_bit) a ^= 0x1b;  // x^8 + x^4 + x^3 + x + 1
        b >>= 1;
    }
    return p;
}

// Key expansion
void aes_init(aes_ctx_t* ctx, const uint8_t* key) {
    int i;
    uint32_t temp;
    
    // First round key is the key itself
    for (i = 0; i < 4; i++) {
        ctx->round_keys[i] = ((uint32_t)key[4*i] << 24) |
                             ((uint32_t)key[4*i+1] << 16) |
                             ((uint32_t)key[4*i+2] << 8) |
                             ((uint32_t)key[4*i+3]);
    }
    
    // Generate remaining round keys
    for (i = 4; i < 44; i++) {
        temp = ctx->round_keys[i - 1];
        
        if (i % 4 == 0) {
            // RotWord
            temp = (temp << 8) | (temp >> 24);
            // SubWord
            temp = ((uint32_t)sbox[(temp >> 24) & 0xff] << 24) |
                   ((uint32_t)sbox[(temp >> 16) & 0xff] << 16) |
                   ((uint32_t)sbox[(temp >> 8) & 0xff] << 8) |
                   ((uint32_t)sbox[temp & 0xff]);
            // XOR with Rcon
            temp ^= ((uint32_t)rcon[i / 4] << 24);
        }
        
        ctx->round_keys[i] = ctx->round_keys[i - 4] ^ temp;
    }
}

// Add round key
static void add_round_key(uint8_t* state, const uint32_t* rk) {
    for (int i = 0; i < 4; i++) {
        state[i*4] ^= (rk[i] >> 24) & 0xff;
        state[i*4+1] ^= (rk[i] >> 16) & 0xff;
        state[i*4+2] ^= (rk[i] >> 8) & 0xff;
        state[i*4+3] ^= rk[i] & 0xff;
    }
}

// SubBytes
static void sub_bytes(uint8_t* state) {
    for (int i = 0; i < 16; i++) {
        state[i] = sbox[state[i]];
    }
}

// InvSubBytes
static void inv_sub_bytes(uint8_t* state) {
    for (int i = 0; i < 16; i++) {
        state[i] = inv_sbox[state[i]];
    }
}

// ShiftRows
static void shift_rows(uint8_t* state) {
    uint8_t temp;
    
    // Row 1: shift left by 1
    temp = state[1];
    state[1] = state[5];
    state[5] = state[9];
    state[9] = state[13];
    state[13] = temp;
    
    // Row 2: shift left by 2
    temp = state[2];
    state[2] = state[10];
    state[10] = temp;
    temp = state[6];
    state[6] = state[14];
    state[14] = temp;
    
    // Row 3: shift left by 3 (= right by 1)
    temp = state[15];
    state[15] = state[11];
    state[11] = state[7];
    state[7] = state[3];
    state[3] = temp;
}

// InvShiftRows
static void inv_shift_rows(uint8_t* state) {
    uint8_t temp;
    
    // Row 1: shift right by 1
    temp = state[13];
    state[13] = state[9];
    state[9] = state[5];
    state[5] = state[1];
    state[1] = temp;
    
    // Row 2: shift right by 2
    temp = state[2];
    state[2] = state[10];
    state[10] = temp;
    temp = state[6];
    state[6] = state[14];
    state[14] = temp;
    
    // Row 3: shift right by 3 (= left by 1)
    temp = state[3];
    state[3] = state[7];
    state[7] = state[11];
    state[11] = state[15];
    state[15] = temp;
}

// MixColumns
static void mix_columns(uint8_t* state) {
    uint8_t a, b, c, d;
    
    for (int i = 0; i < 4; i++) {
        a = state[i*4];
        b = state[i*4+1];
        c = state[i*4+2];
        d = state[i*4+3];
        
        state[i*4]   = gf_mul(a, 2) ^ gf_mul(b, 3) ^ c ^ d;
        state[i*4+1] = a ^ gf_mul(b, 2) ^ gf_mul(c, 3) ^ d;
        state[i*4+2] = a ^ b ^ gf_mul(c, 2) ^ gf_mul(d, 3);
        state[i*4+3] = gf_mul(a, 3) ^ b ^ c ^ gf_mul(d, 2);
    }
}

// InvMixColumns
static void inv_mix_columns(uint8_t* state) {
    uint8_t a, b, c, d;
    
    for (int i = 0; i < 4; i++) {
        a = state[i*4];
        b = state[i*4+1];
        c = state[i*4+2];
        d = state[i*4+3];
        
        state[i*4]   = gf_mul(a, 0x0e) ^ gf_mul(b, 0x0b) ^ gf_mul(c, 0x0d) ^ gf_mul(d, 0x09);
        state[i*4+1] = gf_mul(a, 0x09) ^ gf_mul(b, 0x0e) ^ gf_mul(c, 0x0b) ^ gf_mul(d, 0x0d);
        state[i*4+2] = gf_mul(a, 0x0d) ^ gf_mul(b, 0x09) ^ gf_mul(c, 0x0e) ^ gf_mul(d, 0x0b);
        state[i*4+3] = gf_mul(a, 0x0b) ^ gf_mul(b, 0x0d) ^ gf_mul(c, 0x09) ^ gf_mul(d, 0x0e);
    }
}

void aes_encrypt_block(const aes_ctx_t* ctx, const uint8_t* in, uint8_t* out) {
    uint8_t state[16];
    
    // Copy input to state (column-major order)
    for (int i = 0; i < 16; i++) {
        state[i] = in[i];
    }
    
    // Initial round key addition
    add_round_key(state, &ctx->round_keys[0]);
    
    // Main rounds
    for (int round = 1; round < AES_128_ROUNDS; round++) {
        sub_bytes(state);
        shift_rows(state);
        mix_columns(state);
        add_round_key(state, &ctx->round_keys[round * 4]);
    }
    
    // Final round (no MixColumns)
    sub_bytes(state);
    shift_rows(state);
    add_round_key(state, &ctx->round_keys[AES_128_ROUNDS * 4]);
    
    // Copy state to output
    for (int i = 0; i < 16; i++) {
        out[i] = state[i];
    }
}

void aes_decrypt_block(const aes_ctx_t* ctx, const uint8_t* in, uint8_t* out) {
    uint8_t state[16];
    
    // Copy input to state
    for (int i = 0; i < 16; i++) {
        state[i] = in[i];
    }
    
    // Initial round key addition
    add_round_key(state, &ctx->round_keys[AES_128_ROUNDS * 4]);
    
    // Main rounds (in reverse)
    for (int round = AES_128_ROUNDS - 1; round > 0; round--) {
        inv_shift_rows(state);
        inv_sub_bytes(state);
        add_round_key(state, &ctx->round_keys[round * 4]);
        inv_mix_columns(state);
    }
    
    // Final round
    inv_shift_rows(state);
    inv_sub_bytes(state);
    add_round_key(state, &ctx->round_keys[0]);
    
    // Copy state to output
    for (int i = 0; i < 16; i++) {
        out[i] = state[i];
    }
}

void aes_cbc_encrypt(const aes_ctx_t* ctx, uint8_t* iv, uint8_t* data, size_t len) {
    uint8_t* prev = iv;
    
    for (size_t i = 0; i < len; i += AES_BLOCK_SIZE) {
        // XOR with previous ciphertext (or IV)
        for (int j = 0; j < AES_BLOCK_SIZE; j++) {
            data[i + j] ^= prev[j];
        }
        
        // Encrypt block
        aes_encrypt_block(ctx, &data[i], &data[i]);
        prev = &data[i];
    }
}

void aes_cbc_decrypt(const aes_ctx_t* ctx, uint8_t* iv, uint8_t* data, size_t len) {
    uint8_t prev[AES_BLOCK_SIZE];
    uint8_t curr[AES_BLOCK_SIZE];
    
    // Save IV
    for (int i = 0; i < AES_BLOCK_SIZE; i++) {
        prev[i] = iv[i];
    }
    
    for (size_t i = 0; i < len; i += AES_BLOCK_SIZE) {
        // Save current ciphertext
        for (int j = 0; j < AES_BLOCK_SIZE; j++) {
            curr[j] = data[i + j];
        }
        
        // Decrypt block
        aes_decrypt_block(ctx, &data[i], &data[i]);
        
        // XOR with previous ciphertext (or IV)
        for (int j = 0; j < AES_BLOCK_SIZE; j++) {
            data[i + j] ^= prev[j];
        }
        
        // Update prev
        for (int j = 0; j < AES_BLOCK_SIZE; j++) {
            prev[j] = curr[j];
        }
    }
}

// =============================================================================
// AES-GCM Implementation
// =============================================================================

// GF(2^128) multiplication
static void gf128_mul(uint8_t* x, const uint8_t* y) {
    uint8_t z[16] = {0};
    uint8_t v[16];
    
    for (int i = 0; i < 16; i++) v[i] = y[i];
    
    for (int i = 0; i < 128; i++) {
        if (x[i / 8] & (1 << (7 - (i % 8)))) {
            for (int j = 0; j < 16; j++) z[j] ^= v[j];
        }
        
        // Multiply v by x (shift right and reduce)
        uint8_t carry = v[15] & 1;
        for (int j = 15; j > 0; j--) {
            v[j] = (v[j] >> 1) | ((v[j-1] & 1) << 7);
        }
        v[0] >>= 1;
        
        if (carry) v[0] ^= 0xe1;  // Reduction polynomial
    }
    
    for (int i = 0; i < 16; i++) x[i] = z[i];
}

// Increment counter
static void inc32(uint8_t* counter) {
    for (int i = 15; i >= 12; i--) {
        counter[i]++;
        if (counter[i] != 0) break;
    }
}

void aes_gcm_init(aes_gcm_ctx_t* ctx, const uint8_t* key, const uint8_t* iv, size_t iv_len) {
    uint8_t zero[16] = {0};
    
    aes_init(&ctx->aes, key);
    
    // Compute H = E(K, 0)
    aes_encrypt_block(&ctx->aes, zero, ctx->H);
    
    // Compute J0 (initial counter)
    if (iv_len == 12) {
        // Standard 96-bit IV
        for (int i = 0; i < 12; i++) ctx->J0[i] = iv[i];
        ctx->J0[12] = 0;
        ctx->J0[13] = 0;
        ctx->J0[14] = 0;
        ctx->J0[15] = 1;
    } else {
        // GHASH IV if not 96 bits
        for (int i = 0; i < 16; i++) ctx->J0[i] = 0;
        // This is simplified - full implementation would GHASH the IV
    }
    
    // Set up counter
    for (int i = 0; i < 16; i++) ctx->counter[i] = ctx->J0[i];
}

// GHASH function
static void ghash(const uint8_t* H, const uint8_t* data, size_t len, uint8_t* out) {
    for (int i = 0; i < 16; i++) out[i] = 0;
    
    while (len >= 16) {
        for (int i = 0; i < 16; i++) out[i] ^= data[i];
        gf128_mul(out, H);
        data += 16;
        len -= 16;
    }
    
    // Handle partial block
    if (len > 0) {
        for (size_t i = 0; i < len; i++) out[i] ^= data[i];
        gf128_mul(out, H);
    }
}

void aes_gcm_encrypt(aes_gcm_ctx_t* ctx, const uint8_t* aad, size_t aad_len,
                     uint8_t* data, size_t data_len, uint8_t* tag) {
    uint8_t S[16] = {0};
    uint8_t enc_ctr[16];
    
    // Process AAD
    if (aad_len > 0) {
        ghash(ctx->H, aad, aad_len, S);
    }
    
    // Encrypt data with counter mode
    for (size_t i = 0; i < data_len; i += 16) {
        inc32(ctx->counter);
        aes_encrypt_block(&ctx->aes, ctx->counter, enc_ctr);
        
        size_t block_len = (data_len - i > 16) ? 16 : data_len - i;
        for (size_t j = 0; j < block_len; j++) {
            data[i + j] ^= enc_ctr[j];
        }
    }
    
    // GHASH ciphertext
    ghash(ctx->H, data, data_len, S);
    
    // Add length block
    uint8_t len_block[16] = {0};
    uint64_t aad_bits = aad_len * 8;
    uint64_t data_bits = data_len * 8;
    len_block[0] = (aad_bits >> 56) & 0xff;
    len_block[1] = (aad_bits >> 48) & 0xff;
    len_block[2] = (aad_bits >> 40) & 0xff;
    len_block[3] = (aad_bits >> 32) & 0xff;
    len_block[4] = (aad_bits >> 24) & 0xff;
    len_block[5] = (aad_bits >> 16) & 0xff;
    len_block[6] = (aad_bits >> 8) & 0xff;
    len_block[7] = aad_bits & 0xff;
    len_block[8] = (data_bits >> 56) & 0xff;
    len_block[9] = (data_bits >> 48) & 0xff;
    len_block[10] = (data_bits >> 40) & 0xff;
    len_block[11] = (data_bits >> 32) & 0xff;
    len_block[12] = (data_bits >> 24) & 0xff;
    len_block[13] = (data_bits >> 16) & 0xff;
    len_block[14] = (data_bits >> 8) & 0xff;
    len_block[15] = data_bits & 0xff;
    
    for (int i = 0; i < 16; i++) S[i] ^= len_block[i];
    gf128_mul(S, ctx->H);
    
    // Generate tag
    aes_encrypt_block(&ctx->aes, ctx->J0, enc_ctr);
    for (int i = 0; i < 16; i++) {
        tag[i] = S[i] ^ enc_ctr[i];
    }
}

int aes_gcm_decrypt(aes_gcm_ctx_t* ctx, const uint8_t* aad, size_t aad_len,
                    uint8_t* data, size_t data_len, const uint8_t* tag) {
    uint8_t computed_tag[16];
    
    // First compute tag over ciphertext
    uint8_t S[16] = {0};
    uint8_t enc_ctr[16];
    
    if (aad_len > 0) {
        ghash(ctx->H, aad, aad_len, S);
    }
    ghash(ctx->H, data, data_len, S);
    
    // Add length block
    uint8_t len_block[16] = {0};
    uint64_t aad_bits = aad_len * 8;
    uint64_t data_bits = data_len * 8;
    len_block[0] = (aad_bits >> 56) & 0xff;
    len_block[1] = (aad_bits >> 48) & 0xff;
    len_block[2] = (aad_bits >> 40) & 0xff;
    len_block[3] = (aad_bits >> 32) & 0xff;
    len_block[4] = (aad_bits >> 24) & 0xff;
    len_block[5] = (aad_bits >> 16) & 0xff;
    len_block[6] = (aad_bits >> 8) & 0xff;
    len_block[7] = aad_bits & 0xff;
    len_block[8] = (data_bits >> 56) & 0xff;
    len_block[9] = (data_bits >> 48) & 0xff;
    len_block[10] = (data_bits >> 40) & 0xff;
    len_block[11] = (data_bits >> 32) & 0xff;
    len_block[12] = (data_bits >> 24) & 0xff;
    len_block[13] = (data_bits >> 16) & 0xff;
    len_block[14] = (data_bits >> 8) & 0xff;
    len_block[15] = data_bits & 0xff;
    
    for (int i = 0; i < 16; i++) S[i] ^= len_block[i];
    gf128_mul(S, ctx->H);
    
    aes_encrypt_block(&ctx->aes, ctx->J0, enc_ctr);
    for (int i = 0; i < 16; i++) {
        computed_tag[i] = S[i] ^ enc_ctr[i];
    }
    
    // Verify tag
    int valid = 1;
    for (int i = 0; i < 16; i++) {
        if (computed_tag[i] != tag[i]) valid = 0;
    }
    
    if (!valid) return -1;
    
    // Decrypt data
    for (size_t i = 0; i < data_len; i += 16) {
        inc32(ctx->counter);
        aes_encrypt_block(&ctx->aes, ctx->counter, enc_ctr);
        
        size_t block_len = (data_len - i > 16) ? 16 : data_len - i;
        for (size_t j = 0; j < block_len; j++) {
            data[i + j] ^= enc_ctr[j];
        }
    }
    
    return 0;
}

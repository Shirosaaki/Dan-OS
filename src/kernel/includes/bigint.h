//
// Big Integer Library for RSA
// Supports up to 4096-bit integers
//

#ifndef BIGINT_H
#define BIGINT_H

#include <stdint.h>
#include <stddef.h>

// Max size in 32-bit words (4096 bits = 128 words)
#define BIGINT_MAX_WORDS 128

typedef struct {
    uint32_t words[BIGINT_MAX_WORDS];
    int size;  // Number of significant words
} bigint_t;

// Basic operations
void bigint_init(bigint_t* n);
void bigint_copy(bigint_t* dst, const bigint_t* src);
int bigint_cmp(const bigint_t* a, const bigint_t* b);
int bigint_is_zero(const bigint_t* n);

// Set from bytes (big-endian)
void bigint_from_bytes(bigint_t* n, const uint8_t* bytes, size_t len);
void bigint_to_bytes(const bigint_t* n, uint8_t* bytes, size_t len);

// Set from integer
void bigint_from_int(bigint_t* n, uint32_t val);

// Arithmetic
void bigint_add(bigint_t* result, const bigint_t* a, const bigint_t* b);
void bigint_sub(bigint_t* result, const bigint_t* a, const bigint_t* b);
void bigint_mul(bigint_t* result, const bigint_t* a, const bigint_t* b);

// Modular arithmetic
void bigint_mod(bigint_t* result, const bigint_t* a, const bigint_t* m);
void bigint_modmul(bigint_t* result, const bigint_t* a, const bigint_t* b, const bigint_t* m);
void bigint_modpow(bigint_t* result, const bigint_t* base, const bigint_t* exp, const bigint_t* mod);

// Bit operations
int bigint_bits(const bigint_t* n);
int bigint_get_bit(const bigint_t* n, int bit);
void bigint_shl(bigint_t* n, int bits);
void bigint_shr(bigint_t* n, int bits);

#endif // BIGINT_H

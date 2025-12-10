//
// RSA Public Key Operations
//

#ifndef RSA_H
#define RSA_H

#include <stdint.h>
#include <stddef.h>
#include "bigint.h"

// RSA public key
typedef struct {
    bigint_t n;  // Modulus
    bigint_t e;  // Public exponent
    int bits;    // Key size in bits
} rsa_public_key_t;

// Initialize RSA key from bytes
void rsa_init_public_key(rsa_public_key_t* key, 
                         const uint8_t* n, size_t n_len,
                         const uint8_t* e, size_t e_len);

// RSA encryption (for TLS pre-master secret)
// out must be at least key_bits/8 bytes
int rsa_encrypt(const rsa_public_key_t* key, 
                const uint8_t* in, size_t in_len,
                uint8_t* out, size_t out_size);

// PKCS#1 v1.5 padding for encryption
int rsa_pkcs1_pad(uint8_t* padded, size_t padded_len,
                  const uint8_t* msg, size_t msg_len);

// PKCS#1 v1.5 signature verification (for certificates)
int rsa_pkcs1_verify(const rsa_public_key_t* key,
                     const uint8_t* hash, size_t hash_len,
                     const uint8_t* sig, size_t sig_len);

#endif // RSA_H

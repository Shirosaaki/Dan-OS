//
// Big Integer Implementation for RSA
//

#include <kernel/drivers/bigint.h>

void bigint_init(bigint_t* n) {
    for (int i = 0; i < BIGINT_MAX_WORDS; i++) {
        n->words[i] = 0;
    }
    n->size = 0;
}

void bigint_copy(bigint_t* dst, const bigint_t* src) {
    for (int i = 0; i < BIGINT_MAX_WORDS; i++) {
        dst->words[i] = src->words[i];
    }
    dst->size = src->size;
}

// Normalize: adjust size to actual significant words
static void bigint_normalize(bigint_t* n) {
    while (n->size > 0 && n->words[n->size - 1] == 0) {
        n->size--;
    }
}

void bigint_from_int(bigint_t* n, uint32_t val) {
    bigint_init(n);
    n->words[0] = val;
    n->size = (val != 0) ? 1 : 0;
}

void bigint_from_bytes(bigint_t* n, const uint8_t* bytes, size_t len) {
    bigint_init(n);
    
    // Convert big-endian bytes to little-endian words
    int word_idx = 0;
    int byte_in_word = 0;
    
    for (int i = len - 1; i >= 0 && word_idx < BIGINT_MAX_WORDS; i--) {
        n->words[word_idx] |= ((uint32_t)bytes[i]) << (byte_in_word * 8);
        byte_in_word++;
        if (byte_in_word == 4) {
            byte_in_word = 0;
            word_idx++;
        }
    }
    
    n->size = word_idx + (byte_in_word > 0 ? 1 : 0);
    bigint_normalize(n);
}

void bigint_to_bytes(const bigint_t* n, uint8_t* bytes, size_t len) {
    // Zero the output
    for (size_t i = 0; i < len; i++) {
        bytes[i] = 0;
    }
    
    // Convert little-endian words to big-endian bytes
    size_t byte_idx = len - 1;
    for (int i = 0; i < n->size && byte_idx < len; i++) {
        for (int j = 0; j < 4 && byte_idx < len; j++) {
            bytes[byte_idx--] = (n->words[i] >> (j * 8)) & 0xff;
            if (byte_idx >= len) break;  // Unsigned wraparound check
        }
    }
}

int bigint_is_zero(const bigint_t* n) {
    return n->size == 0;
}

int bigint_cmp(const bigint_t* a, const bigint_t* b) {
    if (a->size != b->size) {
        return (a->size > b->size) ? 1 : -1;
    }
    
    for (int i = a->size - 1; i >= 0; i--) {
        if (a->words[i] != b->words[i]) {
            return (a->words[i] > b->words[i]) ? 1 : -1;
        }
    }
    
    return 0;
}

void bigint_add(bigint_t* result, const bigint_t* a, const bigint_t* b) {
    uint64_t carry = 0;
    int max_size = (a->size > b->size) ? a->size : b->size;
    
    for (int i = 0; i < max_size || carry; i++) {
        uint64_t sum = carry;
        if (i < a->size) sum += a->words[i];
        if (i < b->size) sum += b->words[i];
        
        result->words[i] = (uint32_t)(sum & 0xFFFFFFFF);
        carry = sum >> 32;
        
        if (i >= result->size) result->size = i + 1;
    }
    
    bigint_normalize(result);
}

void bigint_sub(bigint_t* result, const bigint_t* a, const bigint_t* b) {
    int64_t borrow = 0;
    
    for (int i = 0; i < a->size; i++) {
        int64_t diff = (int64_t)a->words[i] - borrow;
        if (i < b->size) diff -= b->words[i];
        
        if (diff < 0) {
            diff += 0x100000000LL;
            borrow = 1;
        } else {
            borrow = 0;
        }
        
        result->words[i] = (uint32_t)diff;
    }
    
    result->size = a->size;
    bigint_normalize(result);
}

void bigint_mul(bigint_t* result, const bigint_t* a, const bigint_t* b) {
    bigint_t temp;
    bigint_init(&temp);
    
    for (int i = 0; i < a->size; i++) {
        uint64_t carry = 0;
        for (int j = 0; j < b->size || carry; j++) {
            int k = i + j;
            if (k >= BIGINT_MAX_WORDS) break;
            
            uint64_t product = temp.words[k] + carry;
            if (j < b->size) {
                product += (uint64_t)a->words[i] * b->words[j];
            }
            
            temp.words[k] = (uint32_t)(product & 0xFFFFFFFF);
            carry = product >> 32;
            
            if (k >= temp.size) temp.size = k + 1;
        }
    }
    
    bigint_normalize(&temp);
    bigint_copy(result, &temp);
}

int bigint_bits(const bigint_t* n) {
    if (n->size == 0) return 0;
    
    int bits = (n->size - 1) * 32;
    uint32_t top = n->words[n->size - 1];
    
    while (top) {
        bits++;
        top >>= 1;
    }
    
    return bits;
}

int bigint_get_bit(const bigint_t* n, int bit) {
    int word_idx = bit / 32;
    int bit_idx = bit % 32;
    
    if (word_idx >= n->size) return 0;
    return (n->words[word_idx] >> bit_idx) & 1;
}

void bigint_shl(bigint_t* n, int bits) {
    if (bits == 0 || n->size == 0) return;
    
    int word_shift = bits / 32;
    int bit_shift = bits % 32;
    
    // Shift words first
    if (word_shift > 0) {
        for (int i = BIGINT_MAX_WORDS - 1; i >= word_shift; i--) {
            n->words[i] = n->words[i - word_shift];
        }
        for (int i = 0; i < word_shift; i++) {
            n->words[i] = 0;
        }
        n->size += word_shift;
        if (n->size > BIGINT_MAX_WORDS) n->size = BIGINT_MAX_WORDS;
    }
    
    // Shift bits within words
    if (bit_shift > 0) {
        uint32_t carry = 0;
        for (int i = 0; i < n->size; i++) {
            uint32_t new_carry = n->words[i] >> (32 - bit_shift);
            n->words[i] = (n->words[i] << bit_shift) | carry;
            carry = new_carry;
        }
        if (carry && n->size < BIGINT_MAX_WORDS) {
            n->words[n->size++] = carry;
        }
    }
    
    bigint_normalize(n);
}

void bigint_shr(bigint_t* n, int bits) {
    if (bits == 0 || n->size == 0) return;
    
    int word_shift = bits / 32;
    int bit_shift = bits % 32;
    
    // Shift words first
    if (word_shift > 0) {
        if (word_shift >= n->size) {
            bigint_init(n);
            return;
        }
        for (int i = 0; i < n->size - word_shift; i++) {
            n->words[i] = n->words[i + word_shift];
        }
        for (int i = n->size - word_shift; i < n->size; i++) {
            n->words[i] = 0;
        }
        n->size -= word_shift;
    }
    
    // Shift bits within words
    if (bit_shift > 0) {
        uint32_t carry = 0;
        for (int i = n->size - 1; i >= 0; i--) {
            uint32_t new_carry = n->words[i] << (32 - bit_shift);
            n->words[i] = (n->words[i] >> bit_shift) | carry;
            carry = new_carry;
        }
    }
    
    bigint_normalize(n);
}

void bigint_mod(bigint_t* result, const bigint_t* a, const bigint_t* m) {
    bigint_t temp;
    bigint_copy(&temp, a);
    
    int a_bits = bigint_bits(&temp);
    int m_bits = bigint_bits(m);
    
    if (a_bits < m_bits || bigint_cmp(&temp, m) < 0) {
        bigint_copy(result, &temp);
        return;
    }
    
    // Shift m left to align with a
    bigint_t shifted_m;
    bigint_copy(&shifted_m, m);
    int shift = a_bits - m_bits;
    bigint_shl(&shifted_m, shift);
    
    // Subtract shifted_m while possible
    for (int i = shift; i >= 0; i--) {
        if (bigint_cmp(&temp, &shifted_m) >= 0) {
            bigint_sub(&temp, &temp, &shifted_m);
        }
        bigint_shr(&shifted_m, 1);
    }
    
    bigint_copy(result, &temp);
}

void bigint_modmul(bigint_t* result, const bigint_t* a, const bigint_t* b, const bigint_t* m) {
    bigint_t temp;
    bigint_mul(&temp, a, b);
    bigint_mod(result, &temp, m);
}

// Modular exponentiation using square-and-multiply
void bigint_modpow(bigint_t* result, const bigint_t* base, const bigint_t* exp, const bigint_t* mod) {
    bigint_t temp_result;
    bigint_t temp_base;
    
    bigint_from_int(&temp_result, 1);
    bigint_copy(&temp_base, base);
    bigint_mod(&temp_base, &temp_base, mod);
    
    int exp_bits = bigint_bits(exp);
    
    for (int i = 0; i < exp_bits; i++) {
        if (bigint_get_bit(exp, i)) {
            bigint_modmul(&temp_result, &temp_result, &temp_base, mod);
        }
        bigint_modmul(&temp_base, &temp_base, &temp_base, mod);
    }
    
    bigint_copy(result, &temp_result);
}

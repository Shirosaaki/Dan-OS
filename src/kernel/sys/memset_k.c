//
// Created by assistant on 01/29/2026.
//

#include <kernel/sys/string.h>

void* memset_k(void* s, int c, size_t n) {
    unsigned char* p = (unsigned char*)s;
    while (n--) {
        *p++ = (unsigned char)c;
    }
    return s;
}
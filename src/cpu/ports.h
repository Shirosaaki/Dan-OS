//
// Created by Shirosaaki on 19/09/25.
//

#ifndef PORTS_H
#define PORTS_H

#include "../cpu/types.h"

unsigned char port_byte_in (u16 port);
void port_byte_out (u16 port, u8 data);
unsigned short port_word_in (u16 port);
void port_word_out (u16 port, u16 data);

// Write a byte to the specified port
static inline void outb(u16 port, u8 val) {
    asm volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

// Read a byte from the specified port
static inline u8 inb(u16 port) {
    u8 ret;
    asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

// Read a word from the specified port
static inline u16 inw(u16 port) {
    u16 ret;
    asm volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

// Write a word to the specified port
static inline void outw(u16 port, u16 val) {
    asm volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

#endif

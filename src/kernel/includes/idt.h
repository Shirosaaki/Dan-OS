//
// Created by Shirosaaki on 21/09/25.
//

#ifndef IDT_H
#define IDT_H

#include "../../cpu/types.h"

// IDT entry structure for 64-bit mode
typedef struct {
    u16 offset_low;    // Lower 16 bits of handler function address
    u16 selector;      // Kernel segment selector
    u8  ist;           // Interrupt Stack Table offset (bits 0-2), rest reserved
    u8  type_attr;     // Type and attributes
    u16 offset_mid;    // Middle 16 bits of handler function address
    u32 offset_high;   // Higher 32 bits of handler function address
    u32 reserved;      // Reserved, must be zero
} __attribute__((packed)) idt_entry_t;

// IDT pointer structure
typedef struct {
    u16 limit;         // Size of IDT - 1
    u64 base;          // Address of IDT
} __attribute__((packed)) idt_ptr_t;

// IDT constants
#define IDT_ENTRIES 256

// Gate types
#define IDT_INTERRUPT_GATE 0x8E
#define IDT_TRAP_GATE      0x8F

// Function prototypes
void idt_init(void);
void idt_set_gate(u8 num, u64 handler, u16 selector, u8 flags);
void idt_install(void);

#endif // IDT_H
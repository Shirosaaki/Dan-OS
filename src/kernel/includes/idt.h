#ifndef IDT_H
#define IDT_H

#include <stdint.h>

// IDT Entry structure
typedef struct {
    uint16_t offset_low;    // Offset bits 0-15
    uint16_t selector;      // Code segment selector
    uint8_t ist;            // Interrupt Stack Table offset
    uint8_t type_attr;      // Type and attributes
    uint16_t offset_mid;    // Offset bits 16-31
    uint32_t offset_high;   // Offset bits 32-63
    uint32_t zero;          // Reserved
} __attribute__((packed)) idt_entry_t;

// IDT Pointer structure
typedef struct {
    uint16_t limit;         // Size of IDT - 1
    uint64_t base;          // Base address of IDT
} __attribute__((packed)) idt_ptr_t;

#define IDT_ENTRIES 256

// Initialize the IDT
void idt_init(void);

// Set an IDT gate
void idt_set_gate(uint8_t num, uint64_t handler, uint16_t selector, uint8_t flags);

// ISR handlers (defined in interrupts.asm)
extern void isr0(void);
extern void isr1(void);
extern void isr2(void);
extern void isr3(void);
extern void isr4(void);
extern void isr5(void);
extern void isr6(void);
extern void isr7(void);
extern void isr8(void);
extern void isr9(void);
extern void isr10(void);
extern void isr11(void);
extern void isr12(void);
extern void isr13(void);
extern void isr14(void);
extern void isr15(void);
extern void isr16(void);
extern void isr17(void);
extern void isr18(void);
extern void isr19(void);
extern void isr20(void);
extern void isr21(void);
extern void isr22(void);
extern void isr23(void);
extern void isr24(void);
extern void isr25(void);
extern void isr26(void);
extern void isr27(void);
extern void isr28(void);
extern void isr29(void);
extern void isr30(void);
extern void isr31(void);

// IRQ handlers (defined in interrupts.asm)
extern void irq0(void);
extern void irq1(void);
extern void irq2(void);
extern void irq3(void);
extern void irq4(void);
extern void irq5(void);
extern void irq6(void);
extern void irq7(void);
extern void irq8(void);
extern void irq9(void);
extern void irq10(void);
extern void irq11(void);
extern void irq12(void);
extern void irq13(void);
extern void irq14(void);
extern void irq15(void);

#endif // IDT_H

//
// Created by Shirosaaki on 21/09/25.
//

#include "../includes/idt.h"
#include "../includes/isr.h"

// IDT table and pointer
static idt_entry_t idt[IDT_ENTRIES];
static idt_ptr_t idt_ptr;

// Set an IDT gate
void idt_set_gate(u8 num, u64 handler, u16 selector, u8 flags) {
    idt[num].offset_low  = (u16)(handler & 0xFFFF);
    idt[num].offset_mid  = (u16)((handler >> 16) & 0xFFFF);
    idt[num].offset_high = (u32)((handler >> 32) & 0xFFFFFFFF);
    idt[num].selector    = selector;
    idt[num].ist         = 0;  // Not using IST for now
    idt[num].type_attr   = flags;
    idt[num].reserved    = 0;
}

// Initialize the IDT
void idt_init(void) {
    // Set up IDT pointer
    idt_ptr.limit = (sizeof(idt_entry_t) * IDT_ENTRIES) - 1;
    idt_ptr.base = (u64)&idt;
    
    // Clear the IDT
    for (int i = 0; i < IDT_ENTRIES; i++) {
        idt_set_gate(i, 0, 0, 0);
    }
    
    // Install exception handlers (ISR 0-31)
    idt_set_gate(0,  (u64)isr0,  0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(1,  (u64)isr1,  0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(2,  (u64)isr2,  0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(3,  (u64)isr3,  0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(4,  (u64)isr4,  0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(5,  (u64)isr5,  0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(6,  (u64)isr6,  0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(7,  (u64)isr7,  0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(8,  (u64)isr8,  0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(9,  (u64)isr9,  0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(10, (u64)isr10, 0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(11, (u64)isr11, 0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(12, (u64)isr12, 0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(13, (u64)isr13, 0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(14, (u64)isr14, 0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(15, (u64)isr15, 0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(16, (u64)isr16, 0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(17, (u64)isr17, 0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(18, (u64)isr18, 0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(19, (u64)isr19, 0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(20, (u64)isr20, 0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(21, (u64)isr21, 0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(22, (u64)isr22, 0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(23, (u64)isr23, 0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(24, (u64)isr24, 0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(25, (u64)isr25, 0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(26, (u64)isr26, 0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(27, (u64)isr27, 0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(28, (u64)isr28, 0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(29, (u64)isr29, 0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(30, (u64)isr30, 0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(31, (u64)isr31, 0x08, IDT_INTERRUPT_GATE);
    
    // Install IRQ handlers (IRQ 0-15 mapped to interrupts 32-47)
    idt_set_gate(32, (u64)irq0,  0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(33, (u64)irq1,  0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(34, (u64)irq2,  0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(35, (u64)irq3,  0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(36, (u64)irq4,  0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(37, (u64)irq5,  0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(38, (u64)irq6,  0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(39, (u64)irq7,  0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(40, (u64)irq8,  0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(41, (u64)irq9,  0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(42, (u64)irq10, 0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(43, (u64)irq11, 0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(44, (u64)irq12, 0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(45, (u64)irq13, 0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(46, (u64)irq14, 0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(47, (u64)irq15, 0x08, IDT_INTERRUPT_GATE);
}

// Install the IDT
void idt_install(void) {
    __asm__ volatile ("lidt %0" : : "m" (idt_ptr));
}
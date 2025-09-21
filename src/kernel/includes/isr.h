//
// Created by Shirosaaki on 21/09/25.
//

#ifndef ISR_H
#define ISR_H

#include "../../cpu/types.h"

// CPU exception numbers
#define ISR_DIVIDE_BY_ZERO          0
#define ISR_DEBUG                   1
#define ISR_NON_MASKABLE_INTERRUPT  2
#define ISR_BREAKPOINT              3
#define ISR_OVERFLOW                4
#define ISR_BOUND_RANGE_EXCEEDED    5
#define ISR_INVALID_OPCODE          6
#define ISR_DEVICE_NOT_AVAILABLE    7
#define ISR_DOUBLE_FAULT            8
#define ISR_COPROCESSOR_SEGMENT     9
#define ISR_INVALID_TSS             10
#define ISR_SEGMENT_NOT_PRESENT     11
#define ISR_STACK_SEGMENT_FAULT     12
#define ISR_GENERAL_PROTECTION      13
#define ISR_PAGE_FAULT              14
#define ISR_RESERVED_15             15
#define ISR_X87_FLOATING_POINT      16
#define ISR_ALIGNMENT_CHECK         17
#define ISR_MACHINE_CHECK           18
#define ISR_SIMD_FLOATING_POINT     19
#define ISR_VIRTUALIZATION          20
#define ISR_CONTROL_PROTECTION      21

// IRQ numbers (start at 32)
#define IRQ0  32  // Timer
#define IRQ1  33  // Keyboard
#define IRQ2  34  // Cascade for slave PIC
#define IRQ3  35  // COM2/COM4
#define IRQ4  36  // COM1/COM3
#define IRQ5  37  // LPT2
#define IRQ6  38  // Floppy disk
#define IRQ7  39  // LPT1
#define IRQ8  40  // CMOS real-time clock
#define IRQ9  41  // Free for peripherals
#define IRQ10 42  // Free for peripherals
#define IRQ11 43  // Free for peripherals
#define IRQ12 44  // PS/2 mouse
#define IRQ13 45  // FPU/Coprocessor/Inter-processor
#define IRQ14 46  // Primary ATA hard disk
#define IRQ15 47  // Secondary ATA hard disk

// Total number of interrupts
#define NUM_INTERRUPTS 256

// Interrupt frame structure (saved by CPU and our ISR stubs)
typedef struct {
    // Pushed by our ISR stub
    u64 r15, r14, r13, r12, r11, r10, r9, r8;
    u64 rbp, rdi, rsi, rdx, rcx, rbx, rax;
    
    // Interrupt number and error code (pushed by our stub)
    u64 int_no, err_code;
    
    // Pushed by CPU automatically
    u64 rip, cs, rflags, rsp, ss;
} __attribute__((packed)) interrupt_frame_t;

// Function pointer type for interrupt handlers
typedef void (*isr_handler_t)(interrupt_frame_t* frame);

// Function prototypes

// Initialize the interrupt system
void isr_init(void);

// Install custom interrupt handler
void isr_install_handler(u8 int_no, isr_handler_t handler);

// Remove interrupt handler
void isr_uninstall_handler(u8 int_no);

// Common interrupt handler (called from assembly stubs)
void isr_handler(interrupt_frame_t* frame);

// IRQ handler (called from assembly stubs)
void irq_handler(interrupt_frame_t* frame);

// Assembly interrupt stubs (defined in isr_stubs.asm)
extern void isr0(void);   // Divide by zero
extern void isr1(void);   // Debug
extern void isr2(void);   // Non-maskable interrupt
extern void isr3(void);   // Breakpoint
extern void isr4(void);   // Overflow
extern void isr5(void);   // Bound range exceeded
extern void isr6(void);   // Invalid opcode
extern void isr7(void);   // Device not available
extern void isr8(void);   // Double fault
extern void isr9(void);   // Coprocessor segment overrun
extern void isr10(void);  // Invalid TSS
extern void isr11(void);  // Segment not present
extern void isr12(void);  // Stack segment fault
extern void isr13(void);  // General protection fault
extern void isr14(void);  // Page fault
extern void isr15(void);  // Reserved
extern void isr16(void);  // x87 floating point exception
extern void isr17(void);  // Alignment check
extern void isr18(void);  // Machine check
extern void isr19(void);  // SIMD floating point exception
extern void isr20(void);  // Virtualization exception
extern void isr21(void);  // Control protection exception
extern void isr22(void);  // Reserved
extern void isr23(void);  // Reserved
extern void isr24(void);  // Reserved
extern void isr25(void);  // Reserved
extern void isr26(void);  // Reserved
extern void isr27(void);  // Reserved
extern void isr28(void);  // Reserved
extern void isr29(void);  // Reserved
extern void isr30(void);  // Reserved
extern void isr31(void);  // Reserved

// IRQ stubs
extern void irq0(void);   // Timer
extern void irq1(void);   // Keyboard
extern void irq2(void);   // Cascade
extern void irq3(void);   // COM2/COM4
extern void irq4(void);   // COM1/COM3
extern void irq5(void);   // LPT2
extern void irq6(void);   // Floppy
extern void irq7(void);   // LPT1
extern void irq8(void);   // CMOS RTC
extern void irq9(void);   // Peripherals
extern void irq10(void);  // Peripherals
extern void irq11(void);  // Peripherals
extern void irq12(void);  // PS/2 Mouse
extern void irq13(void);  // FPU
extern void irq14(void);  // Primary ATA
extern void irq15(void);  // Secondary ATA

#endif // ISR_H

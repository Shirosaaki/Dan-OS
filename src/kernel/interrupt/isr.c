//
// Created by Shirosaaki on 21/09/25.
//

#include "../includes/isr.h"
#include "../includes/tty.h"

// Array of interrupt handlers
static isr_handler_t interrupt_handlers[NUM_INTERRUPTS];

// Exception messages
static const char* exception_messages[] = {
    "Division By Zero",
    "Debug",
    "Non Maskable Interrupt",
    "Breakpoint",
    "Into Detected Overflow",
    "Out of Bounds",
    "Invalid Opcode",
    "No Coprocessor",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Bad TSS",
    "Segment Not Present",
    "Stack Fault",
    "General Protection Fault",
    "Page Fault",
    "Unknown Interrupt",
    "Coprocessor Fault",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating-Point Exception",
    "Virtualization Exception",
    "Control Protection Exception",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved"
};

// Port I/O functions for PIC (Programmable Interrupt Controller)
static inline void outb(u16 port, u8 val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline u8 inb(u16 port) {
    u8 ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

// PIC ports
#define PIC1_COMMAND    0x20
#define PIC1_DATA       0x21
#define PIC2_COMMAND    0xA0
#define PIC2_DATA       0xA1
#define PIC_EOI         0x20

// Send End of Interrupt to PIC
static void pic_send_eoi(u8 irq) {
    if (irq >= 8) {
        outb(PIC2_COMMAND, PIC_EOI);
    }
    outb(PIC1_COMMAND, PIC_EOI);
}

// Initialize the interrupt system
void isr_init(void) {
    // Clear all interrupt handlers
    for (int i = 0; i < NUM_INTERRUPTS; i++) {
        interrupt_handlers[i] = 0;
    }
    
    // Remap the PIC (Programmable Interrupt Controller)
    // Default mappings:
    // PIC1: IRQ 0-7  -> INT 0x08-0x0F
    // PIC2: IRQ 8-15 -> INT 0x70-0x77
    // We remap to:
    // PIC1: IRQ 0-7  -> INT 0x20-0x27
    // PIC2: IRQ 8-15 -> INT 0x28-0x2F
    
    // Save masks
    u8 mask1 = inb(PIC1_DATA);
    u8 mask2 = inb(PIC2_DATA);
    
    // Start initialization sequence
    outb(PIC1_COMMAND, 0x11);
    outb(PIC2_COMMAND, 0x11);
    
    // Set vector offsets
    outb(PIC1_DATA, 0x20);  // Master PIC vector offset
    outb(PIC2_DATA, 0x28);  // Slave PIC vector offset
    
    // Tell Master PIC that there is a slave PIC at IRQ2
    outb(PIC1_DATA, 0x04);
    // Tell Slave PIC its cascade identity
    outb(PIC2_DATA, 0x02);
    
    // Set mode to 8086
    outb(PIC1_DATA, 0x01);
    outb(PIC2_DATA, 0x01);
    
    // Restore masks
    outb(PIC1_DATA, mask1);
    outb(PIC2_DATA, mask2);
}

// Install custom interrupt handler
void isr_install_handler(u8 int_no, isr_handler_t handler) {
    if (int_no < NUM_INTERRUPTS) {
        interrupt_handlers[int_no] = handler;
    }
}

// Remove interrupt handler
void isr_uninstall_handler(u8 int_no) {
    if (int_no < NUM_INTERRUPTS) {
        interrupt_handlers[int_no] = 0;
    }
}

// Common interrupt handler (called from assembly stubs)
void isr_handler(interrupt_frame_t* frame) {
    // Check if we have a custom handler for this interrupt
    if (interrupt_handlers[frame->int_no] != 0) {
        isr_handler_t handler = interrupt_handlers[frame->int_no];
        handler(frame);
    } else {
        // Default exception handling
        if (frame->int_no < 32) {
            tty_putstr("Exception: ");
            tty_putstr(exception_messages[frame->int_no]);
            tty_putstr("\n");
            tty_putstr("Error code: ");
            // Simple hex printing (you might want to implement proper hex printing)
            char hex_str[17];
            u64 err = frame->err_code;
            hex_str[16] = '\0';
            for (int i = 15; i >= 0; i--) {
                u8 digit = err & 0xF;
                hex_str[i] = (digit < 10) ? ('0' + digit) : ('A' + digit - 10);
                err >>= 4;
            }
            tty_putstr("0x");
            tty_putstr(hex_str);
            tty_putstr("\n");
            
            // Halt the system for unhandled exceptions
            __asm__ volatile ("cli; hlt");
        } else {
            tty_putstr("Unhandled interrupt: ");
            // Print interrupt number
            char int_str[4];
            u8 int_no = frame->int_no;
            int_str[3] = '\0';
            for (int i = 2; i >= 0; i--) {
                int_str[i] = '0' + (int_no % 10);
                int_no /= 10;
            }
            tty_putstr(int_str);
            tty_putstr("\n");
        }
    }
}

// IRQ handler (called from assembly stubs for IRQs)
void irq_handler(interrupt_frame_t* frame) {
    // Check if we have a custom handler for this IRQ
    if (interrupt_handlers[frame->int_no] != 0) {
        isr_handler_t handler = interrupt_handlers[frame->int_no];
        handler(frame);
    }
    
    // Send End of Interrupt to PIC
    pic_send_eoi(frame->int_no - 32);  // IRQs start at interrupt 32
}

// Default exception handlers that can be installed
void divide_by_zero_handler(interrupt_frame_t* frame) {
    tty_putstr("FATAL: Division by zero at RIP: ");
    // You can add more detailed debugging info here
    __asm__ volatile ("cli; hlt");
}

void page_fault_handler(interrupt_frame_t* frame) {
    // Get the faulting address from CR2
    u64 faulting_address;
    __asm__ volatile ("mov %%cr2, %0" : "=r" (faulting_address));
    
    tty_putstr("Page fault at address: ");
    // Print faulting address (you might want better hex printing)
    tty_putstr("0x");
    char hex_str[17];
    u64 addr = faulting_address;
    hex_str[16] = '\0';
    for (int i = 15; i >= 0; i--) {
        u8 digit = addr & 0xF;
        hex_str[i] = (digit < 10) ? ('0' + digit) : ('A' + digit - 10);
        addr >>= 4;
    }
    tty_putstr(hex_str);
    tty_putstr("\n");
    
    tty_putstr("Error code: ");
    if (frame->err_code & 0x1) tty_putstr("Present ");
    if (frame->err_code & 0x2) tty_putstr("Write ");
    else tty_putstr("Read ");
    if (frame->err_code & 0x4) tty_putstr("User ");
    else tty_putstr("Supervisor ");
    tty_putstr("\n");
    
    __asm__ volatile ("cli; hlt");
}

void general_protection_fault_handler(interrupt_frame_t* frame) {
    tty_putstr("General Protection Fault!\n");
    tty_putstr("Error code: ");
    // Print error code
    char hex_str[17];
    u64 err = frame->err_code;
    hex_str[16] = '\0';
    for (int i = 15; i >= 0; i--) {
        u8 digit = err & 0xF;
        hex_str[i] = (digit < 10) ? ('0' + digit) : ('A' + digit - 10);
        err >>= 4;
    }
    tty_putstr("0x");
    tty_putstr(hex_str);
    tty_putstr("\n");
    
    __asm__ volatile ("cli; hlt");
}

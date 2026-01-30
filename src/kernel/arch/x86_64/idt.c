//
// Created by Shirosaaki on 02/10/2025.
//

#include <kernel/arch/x86_64/idt.h>
#include <kernel/sys/tty.h>
#include <cpu/ports.h>
#include <kernel/arch/x86_64/vmm.h>

// IDT entries and pointer
static idt_entry_t idt[IDT_ENTRIES];
static idt_ptr_t idt_ptr;

// PIC ports
#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA    0xA1

// PIC initialization
static void pic_remap(void) {
    // Save masks
    uint8_t mask1 = inb(PIC1_DATA);
    uint8_t mask2 = inb(PIC2_DATA);

    // Start initialization sequence
    outb(PIC1_COMMAND, 0x11);
    outb(PIC2_COMMAND, 0x11);
    
    // Set vector offsets (32-47)
    outb(PIC1_DATA, 32);
    outb(PIC2_DATA, 40);
    
    // Configure cascading
    outb(PIC1_DATA, 0x04);
    outb(PIC2_DATA, 0x02);
    
    // Set 8086 mode
    outb(PIC1_DATA, 0x01);
    outb(PIC2_DATA, 0x01);
    
    // Restore masks
    outb(PIC1_DATA, mask1);
    outb(PIC2_DATA, mask2);
}

// Set an IDT gate
void idt_set_gate(uint8_t num, uint64_t handler, uint16_t selector, uint8_t flags) {
    idt[num].offset_low = handler & 0xFFFF;
    idt[num].selector = selector;
    idt[num].ist = 0;
    idt[num].type_attr = flags;
    idt[num].offset_mid = (handler >> 16) & 0xFFFF;
    idt[num].offset_high = (handler >> 32) & 0xFFFFFFFF;
    idt[num].zero = 0;
}

// Load IDT (defined in assembly)
extern void idt_load(uint64_t);

// Initialize the IDT
void idt_init(void) {
    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base = (uint64_t)&idt;

    // Clear the IDT
    for (int i = 0; i < IDT_ENTRIES; i++) {
        idt[i].offset_low = 0;
        idt[i].selector = 0;
        idt[i].ist = 0;
        idt[i].type_attr = 0;
        idt[i].offset_mid = 0;
        idt[i].offset_high = 0;
        idt[i].zero = 0;
    }

    // Remap PIC
    pic_remap();

    // Program PIT for timer interrupts (~100Hz)
    outb(0x43, 0x36); // channel 0, mode 3, binary
    outb(0x40, 0x9B); // divisor low
    outb(0x40, 0x2E); // divisor high

    // Unmask timer interrupt (IRQ0)
    outb(PIC1_DATA, 0xFE);

    // Install CPU exception handlers (ISRs)
    idt_set_gate(0, (uint64_t)isr0, 0x08, 0x8E);
    idt_set_gate(1, (uint64_t)isr1, 0x08, 0x8E);
    idt_set_gate(2, (uint64_t)isr2, 0x08, 0x8E);
    idt_set_gate(3, (uint64_t)isr3, 0x08, 0x8E);
    idt_set_gate(4, (uint64_t)isr4, 0x08, 0x8E);
    idt_set_gate(5, (uint64_t)isr5, 0x08, 0x8E);
    idt_set_gate(6, (uint64_t)isr6, 0x08, 0x8E);
    idt_set_gate(7, (uint64_t)isr7, 0x08, 0x8E);
    idt_set_gate(8, (uint64_t)isr8, 0x08, 0x8E);
    idt_set_gate(9, (uint64_t)isr9, 0x08, 0x8E);
    idt_set_gate(10, (uint64_t)isr10, 0x08, 0x8E);
    idt_set_gate(11, (uint64_t)isr11, 0x08, 0x8E);
    idt_set_gate(12, (uint64_t)isr12, 0x08, 0x8E);
    idt_set_gate(13, (uint64_t)isr13, 0x08, 0x8E);
    idt_set_gate(14, (uint64_t)isr14, 0x08, 0x8E);
    idt_set_gate(15, (uint64_t)isr15, 0x08, 0x8E);
    idt_set_gate(16, (uint64_t)isr16, 0x08, 0x8E);
    idt_set_gate(17, (uint64_t)isr17, 0x08, 0x8E);
    idt_set_gate(18, (uint64_t)isr18, 0x08, 0x8E);
    idt_set_gate(19, (uint64_t)isr19, 0x08, 0x8E);
    idt_set_gate(20, (uint64_t)isr20, 0x08, 0x8E);
    idt_set_gate(21, (uint64_t)isr21, 0x08, 0x8E);
    idt_set_gate(22, (uint64_t)isr22, 0x08, 0x8E);
    idt_set_gate(23, (uint64_t)isr23, 0x08, 0x8E);
    idt_set_gate(24, (uint64_t)isr24, 0x08, 0x8E);
    idt_set_gate(25, (uint64_t)isr25, 0x08, 0x8E);
    idt_set_gate(26, (uint64_t)isr26, 0x08, 0x8E);
    idt_set_gate(27, (uint64_t)isr27, 0x08, 0x8E);
    idt_set_gate(28, (uint64_t)isr28, 0x08, 0x8E);
    idt_set_gate(29, (uint64_t)isr29, 0x08, 0x8E);
    idt_set_gate(30, (uint64_t)isr30, 0x08, 0x8E);
    idt_set_gate(31, (uint64_t)isr31, 0x08, 0x8E);

    // Install IRQ handlers (IRQs 0-15 mapped to interrupts 32-47)
    idt_set_gate(32, (uint64_t)irq0, 0x08, 0x8E);
    idt_set_gate(33, (uint64_t)irq1, 0x08, 0x8E);
    idt_set_gate(34, (uint64_t)irq2, 0x08, 0x8E);
    idt_set_gate(35, (uint64_t)irq3, 0x08, 0x8E);
    idt_set_gate(36, (uint64_t)irq4, 0x08, 0x8E);
    idt_set_gate(37, (uint64_t)irq5, 0x08, 0x8E);
    idt_set_gate(38, (uint64_t)irq6, 0x08, 0x8E);
    idt_set_gate(39, (uint64_t)irq7, 0x08, 0x8E);
    idt_set_gate(40, (uint64_t)irq8, 0x08, 0x8E);
    idt_set_gate(41, (uint64_t)irq9, 0x08, 0x8E);
    idt_set_gate(42, (uint64_t)irq10, 0x08, 0x8E);
    idt_set_gate(43, (uint64_t)irq11, 0x08, 0x8E);
    idt_set_gate(44, (uint64_t)irq12, 0x08, 0x8E);
    idt_set_gate(45, (uint64_t)irq13, 0x08, 0x8E);
    idt_set_gate(46, (uint64_t)irq14, 0x08, 0x8E);
    idt_set_gate(47, (uint64_t)irq15, 0x08, 0x8E);

    // Load the IDT
    idt_load((uint64_t)&idt_ptr);

    // Enable interrupts
    __asm__ volatile("sti");
}

// ISR handler
void isr_handler(uint64_t int_no, uint64_t error_code, uint64_t *frame) {
    if (int_no == 14) {
        /* Minimal, safe page-fault handler: print error, CR2 and RIP, then halt.
           Avoid dereferencing page-tables here to prevent boot-time faults. */
        tty_putstr("Page fault (int 14) error_code=");
        tty_puthex64(error_code);
        tty_putstr(" cr2=");
        uint64_t cr2;
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        tty_puthex64(cr2);
        if (frame) {
            tty_putstr(" rip=");
            tty_puthex64(frame[0]);
        }
        tty_putstr(" — halting.\n");
        __asm__ volatile("cli; hlt");
    } else if (int_no == 13) {
        /* General Protection Fault: print basic info and halt. */
        tty_putstr("General Protection Fault (int 13) error_code=");
        tty_puthex64(error_code);
        if (frame) {
            tty_putstr(" rip="); tty_puthex64(frame[0]);
            tty_putstr(" cs="); tty_puthex64(frame[1]);
        }
        tty_putstr(" — halting.\n");
        __asm__ volatile("cli; hlt");
        tty_putstr("General Protection Fault (int 13) error_code=");
        tty_puthex64(error_code);
        uint64_t rip = 0, cs = 0;
        if (frame) {
            rip = frame[0];
            cs  = frame[1];
            tty_putstr(" rip=");
            tty_puthex64(rip);
            tty_putstr(" cs=");
            tty_puthex64(cs);
        }
        uint64_t cr3 = vmm_get_cr3();
        tty_putstr(" cr3="); tty_puthex64(cr3);

        // Page table walk for rip
        if (rip) {
            uint64_t *pml4 = (uint64_t *)(uintptr_t)cr3;
            size_t i4 = (rip >> 39) & 0x1FF;
            uint64_t pml4val = pml4[i4];
            tty_putstr(" pml4["); tty_putdec(i4); tty_putstr("]="); tty_puthex64(pml4val);
            if (pml4val & VMM_PFLAG_PRESENT) {
                uint64_t *pdpe = (uint64_t *)(uintptr_t)(pml4val & 0x000ffffffffff000ULL);
                size_t i3 = (rip >> 30) & 0x1FF;
                uint64_t pdpval = pdpe[i3];
                tty_putstr(" pdp["); tty_putdec(i3); tty_putstr("]="); tty_puthex64(pdpval);
                if (pdpval & VMM_PFLAG_PRESENT) {
                    uint64_t *pde = (uint64_t *)(uintptr_t)(pdpval & 0x000ffffffffff000ULL);
                    size_t i2 = (rip >> 21) & 0x1FF;
                    uint64_t pdeval = pde[i2];
                    tty_putstr(" pd["); tty_putdec(i2); tty_putstr("]="); tty_puthex64(pdeval);
                    if (pdeval & VMM_PFLAG_PRESENT) {
                        uint64_t *pte = (uint64_t *)(uintptr_t)(pdeval & 0x000ffffffffff000ULL);
                        size_t i1 = (rip >> 12) & 0x1FF;
                        uint64_t pteval = pte[i1];
                        tty_putstr(" pt["); tty_putdec(i1); tty_putstr("]="); tty_puthex64(pteval);
                        if (pteval & VMM_PFLAG_PRESENT) {
                            uint64_t paddr = pteval & 0x000ffffffffff000ULL;
                            tty_putstr(" -> phys="); tty_puthex64(paddr);
                        }
                    }
                }
            }
        }

        tty_putstr(" — halting.\n");
        __asm__ volatile("cli; hlt");
    }

    tty_putstr("Received interrupt: ");
    // Simple number printing (for debugging)
    char num[3];
    num[0] = '0' + (int_no / 10);
    num[1] = '0' + (int_no % 10);
    num[2] = '\0';
    tty_putstr(num);
    tty_putstr("\n");
}

// External keyboard handler
extern void keyboard_handler(void);

// IRQ handler
void irq_handler(uint64_t irq_no) {
    // Handle specific IRQs
    if (irq_no == 33) {
        // IRQ1 - Keyboard
        keyboard_handler();
    }
    
    // Send End of Interrupt (EOI) to PIC
    if (irq_no >= 40) {
        // Send EOI to slave PIC
        outb(PIC2_COMMAND, 0x20);
    }
    // Send EOI to master PIC
    outb(PIC1_COMMAND, 0x20);
}

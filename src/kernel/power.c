//
// Created by automated patch on reboot/shutdown feature
//

#include "power.h"
#include "../cpu/ports.h"
#include "tty.h"

// Attempt to reboot using keyboard controller (works on many BIOS/PCs)
void kernel_reboot(void) {
    // Disable interrupts
    __asm__ volatile ("cli");

    // Try using keyboard controller (0x64) pulse
    outb(0x64, 0xFE);

    // If that doesn't work, attempt triple fault by loading invalid IDT
    // Create an empty IDT and load it to force triple fault
    struct {
        uint16_t limit;
        uint64_t base;
    } __attribute__((packed)) idt_ptr = {0, 0};

    __asm__ volatile ("lidt %0" : : "m" (idt_ptr));

    // Halt (system should reboot before reaching here)
    for (;;) __asm__ volatile ("hlt");
}

// Attempt to power off the machine. Try several methods and fall back to HLT.
void kernel_shutdown(void) {
    // Disable interrupts
    __asm__ volatile ("cli");

    // Try ACPI poweroff via PM1a_CNT (common in QEMU/Bochs)
    // Many emulators respond to writes to 0x604 or 0x400
    // Try 0x604 method first
    outw(0x604, 0x2000);

    // Try bochs/qemu port (0xB004)
    outw(0xB004, 0x2000);

    // Try legacy shutdown port
    outw(0x400, 0x2000);

    // If none of the above worked, halt the CPU
    for (;;) __asm__ volatile ("hlt");
}

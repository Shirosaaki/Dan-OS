//
// Created by dan13615 on 11/15/24.
//

#include "tty.h"
#include "idt.h"
#include "keyboard.h"

void kernel_main(void) {
    // Initialize terminal
    tty_init();
    tty_putstr("Welcome to DanOS!\n");
    tty_putstr("Initializing interrupts...\n");
    
    // Initialize IDT and enable interrupts
    idt_init();
    tty_putstr("IDT initialized.\n");
    
    // Initialize keyboard
    keyboard_init();
    tty_putstr("Keyboard initialized.\n");
    tty_putstr("\nType 'help' for available commands.\n");
    tty_putstr("> ");
    
    // Infinite loop - interrupts will handle keyboard input
    while (1) {
        __asm__ volatile("hlt"); // Halt until next interrupt
    }
}
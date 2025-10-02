//
// Created by dan13615 on 11/15/24.
//

#include "tty.h"
#include "idt.h"
#include "keyboard.h"
#include "ata.h"
#include "fat32.h"

void kernel_main(void) {
    // Initialize terminal
    tty_init();
    tty_putstr("Welcome to DanOS!\n");
    tty_putstr("=================\n\n");
    
    // Initialize interrupts
    tty_putstr("Initializing interrupts...\n");
    idt_init();
    tty_putstr("IDT initialized.\n");
    
    // Initialize keyboard
    keyboard_init();
    tty_putstr("Keyboard initialized.\n\n");
    
    // Initialize ATA disk driver
    ata_init();
    
    // Initialize FAT32 filesystem
    if (fat32_init() == 0) {
        tty_putstr("\nFilesystem ready. Type 'ls' to list files.\n");
    } else {
        tty_putstr("\nWarning: Filesystem initialization failed.\n");
        tty_putstr("Disk commands may not work.\n");
    }
    
    tty_putstr("\nType 'help' for available commands.\n");
    tty_putstr("> ");
    
    // Infinite loop - interrupts will handle keyboard input
    while (1) {
        __asm__ volatile("hlt"); // Halt until next interrupt
    }
}
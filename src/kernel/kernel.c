//
// Created by dan13615 on 11/15/24.
//

#include "tty.h"
#include "idt.h"
#include "keyboard.h"
#include "ata.h"
#include "fat32.h"
#include "rtc.h"

void kernel_main(void) {
    // Initialize terminal
    tty_init();
    tty_putstr("Welcome to DanOS!\n");
    tty_putstr("=================\n\n");
    // Initialize interrupts
    idt_init();
    // Initialize keyboard
    keyboard_init();
    // Initialize RTC
    rtc_init();
    // Initialize ATA disk driver
    ata_init();
    // Initialize FAT32 filesystem
    if (fat32_init() != 0) {
        tty_putstr("\nWarning: Filesystem initialization failed.\n");
        tty_putstr("Disk commands may not work.\n");
    } else {
        // Initialize timezone system (requires filesystem)
        timezone_init();
    }
    tty_putstr("DanOS:/$ ");
    
    while (1) {
        __asm__ volatile("hlt"); // Halt until next interrupt
    }
}
//
// Created by dan13615 on 11/15/24.
//

#include "tty.h"
#include "idt.h"
#include "keyboard.h"
#include "ata.h"
#include "fat32.h"
#include "rtc.h"
#include "string.h"
#include <stdint.h>
#include "pmm.h"
#include "vmm.h"
#include "scheduler.h"
#include "syscall.h"

void kernel_main(void *multiboot_info) {
    // Initialize terminal
    tty_init();
    
    tty_putstr("Welcome to DanOS!\n");
    tty_putstr("=================\n\n");
    tty_putstr("DanOS:/$ ");
    // Set initial prompt position for history navigation
    tty_set_prompt_position();
    // Initialize interrupts
    idt_init();
    // Initialize keyboard
    keyboard_init();
    // Initialize RTC
    rtc_init();
    // Initialize ATA disk driver
    ata_init();
    // Initialize physical memory manager (bitmap allocator)
    pmm_init(multiboot_info, 0);
    // Initialize virtual memory manager (page table helpers)
    vmm_init();
    // Initialize scheduler
    scheduler_init();
    // Initialize syscall mechanism
    syscall_init();
    // Add a simple test kernel thread
    extern void test_thread(void);
    scheduler_add_task(test_thread);
    // Initialize FAT32 filesystem
    if (fat32_init() != 0) {
        tty_putstr("\nWarning: Filesystem initialization failed.\n");
        tty_putstr("Disk commands may not work.\n");
    }
    // Initialize timezone system (requires filesystem)
    timezone_init();
    while (1) {
        __asm__ volatile("hlt"); // Halt until next interrupt
    }
}

void test_thread(void) {
    while (1) {
        tty_putstr("[thread] tick\n");
        for (volatile int i = 0; i < 1000000; ++i) __asm__ volatile ("nop");
    }
}
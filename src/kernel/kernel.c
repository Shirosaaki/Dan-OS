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
#include "framebuffer.h"
#include "net.h"
#include "e1000.h"
#include "tcp.h"
#include "dns.h"
#include "usb.h"

void kernel_main(void *multiboot_info) {
    // Try to initialize framebuffer from Multiboot2 info
    // The bootloader now maps 4GB of memory, so framebuffer should be accessible
    int fb_ok = fb_init_from_multiboot2(multiboot_info);
    
    if (fb_ok == 0 && fb_is_available()) {
        // Framebuffer available - initialize terminal with it
        terminal_init();
    }
    
    // Initialize TTY (will use framebuffer if available, else VGA fallback)
    tty_init();
    
    tty_putstr("Welcome to DanOS!\n");
    tty_putstr("=================\n\n");
    
    // Display mode info
    if (fb_is_available()) {
        const framebuffer_info_t* fb = fb_get_info();
        tty_putstr("Framebuffer: ");
        tty_putdec(fb->width);
        tty_putstr("x");
        tty_putdec(fb->height);
        tty_putstr("x");
        tty_putdec(fb->bpp);
        tty_putstr("bpp\n");
    } else {
        tty_putstr("Using VGA text mode\n");
    }
    
    tty_putstr("\nDanOS:/$ ");
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
    // Initialize network stack
    net_init();
    // Initialize E1000 network card
    if (e1000_init() != 0) {
        tty_putstr("Warning: No network card detected.\n");
    }
    // Initialize TCP stack
    tcp_init();
    // Initialize DNS resolver
    dns_init();
    // Initialize USB subsystem (includes PCI scan and USB controller init)
    usb_init();
    
    // Main loop - poll USB devices
    while (1) {
        // Poll USB for keyboard input (works alongside PS/2)
        usb_poll();
        __asm__ volatile("hlt"); // Halt until next interrupt
    }
}

void test_thread(void) {
    while (1) {
        tty_putstr("[thread] tick\n");
        for (volatile int i = 0; i < 1000000; ++i) __asm__ volatile ("nop");
    }
}
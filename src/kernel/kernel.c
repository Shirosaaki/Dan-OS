//
// Created by dan13615 on 11/15/24.
//

#include <kernel/sys/tty.h>
#include <kernel/arch/x86_64/idt.h>
#include <kernel/drivers/keyboard.h>
#include <kernel/drivers/ata.h>
#include <kernel/fs/fat32.h>
#include <kernel/drivers/rtc.h>
#include <kernel/sys/string.h>
#include <stdint.h>
#include <kernel/arch/x86_64/pmm.h>
#include <kernel/arch/x86_64/vmm.h>
#include <kernel/sys/scheduler.h>
#include <kernel/sys/syscall.h>
#include <kernel/drivers/framebuffer.h>
#include <kernel/net/net.h>
#include <kernel/drivers/e1000.h>
#include <kernel/net/tcp.h>
#include <kernel/net/dns.h>
#include <kernel/drivers/usb.h>
#include <cpu/gdt.h>

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
    // Initialize GDT and TSS
    gdt_init();
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
    
    // Disable timer interrupt (IRQ0) for now while debugging user mode
    extern u8 port_byte_in(u16);
    extern void port_byte_out(u16, u8);
    u8 pic_mask = port_byte_in(0x21);
    pic_mask |= 0x01;  // Disable IRQ0 (timer)
    port_byte_out(0x21, pic_mask);
    tty_putstr("[KERNEL] Timer interrupt disabled\n");
    
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
//
// Created by dan13615 on 11/15/24.
//

#include "tty.h"
#include "idt.h"
#include "keyboard.h"
#include "ata.h"
#include "fat32.h"
#include "rtc.h"
#include "framebuffer.h"
#include "string.h"
#include "mouse.h"
#include <stdint.h>
#include "pmm.h"
#include "vmm.h"

// forward declare fb functions
int fb_init(void *multiboot_info_ptr);
void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void fb_putc(uint32_t x, uint32_t y, char c, uint32_t color);
void fb_get_info(framebuffer_info_t *out);

static void fb_puts(uint32_t x, uint32_t y, const char *s, uint32_t color) {
    while (*s) {
        fb_putc(x, y, *s++, color);
        x += 8;
    }
}

void fb_function(void *multiboot_info) {
    if (fb_init(multiboot_info) == 0) {
        framebuffer_info_t info;
        fb_get_info(&info);
        tty_putstr("FB: ");
        tty_putstr("w=");
        {
            char tmp[12]; int pos=0; uint32_t v=info.width; if (v==0) tmp[pos++]='0'; else { char rev[12]; int r=0; while(v){rev[r++]= '0'+(v%10); v/=10;} while(r--) tmp[pos++]=rev[r]; }
            tmp[pos]=0; tty_putstr(tmp);
        }
        tty_putstr(" h=");
        {
            char tmp[12]; int pos=0; uint32_t v=info.height; if (v==0) tmp[pos++]='0'; else { char rev[12]; int r=0; while(v){rev[r++]= '0'+(v%10); v/=10;} while(r--) tmp[pos++]=rev[r]; } tmp[pos]=0; tty_putstr(tmp);
        }
        tty_putstr(" bpp=");
        {
            char tmp[6]; int pos=0; uint32_t v=info.bpp; if (v==0) tmp[pos++]='0'; else { char rev[6]; int r=0; while(v){rev[r++]= '0'+(v%10); v/=10;} while(r--) tmp[pos++]=rev[r]; } tmp[pos]=0; tty_putstr(tmp);
        }
        tty_putstr(" pitch=");
        {
            char tmp[12]; int pos=0; uint32_t v=info.pitch; if (v==0) tmp[pos++]='0'; else { char rev[12]; int r=0; while(v){rev[r++]= '0'+(v%10); v/=10;} while(r--) tmp[pos++]=rev[r]; } tmp[pos]=0; tty_putstr(tmp);
        }
        tty_putstr("\n");
        fb_fill_rect(0, 0, info.width, info.height, 0x00000000);
        mouse_init();
        const char *msg = "Hello from DAN !";
        uint32_t tiny_char_w = 4;
        uint32_t x = (info.width / 2) - (tiny_char_w * (uint32_t)(strlength(msg) / 2));
        uint32_t y = info.height / 2 - 2;
        extern void fb_puts_tiny(uint32_t, uint32_t, const char*, uint32_t);
        fb_puts_tiny(x, y, msg, 0x00FFA500);
    }
}

void kernel_main(void *multiboot_info) {
    // Initialize terminal
    tty_init();
    
    tty_putstr("Welcome to DanOS!\n");
    tty_putstr("=================\n\n");
    //fb_function(multiboot_info);
    tty_putstr("DanOS:/$ ");
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
//
// Created by dan13615 on 11/15/24.
//

#include <stddef.h>
#include "string.h"
#include "tty.h"
#include "vga.h"
#include "../../cpu/ports.h"

// Make these non-static so they can be accessed from commands.c
size_t tty_row;
size_t tty_column;
uint8_t tty_color;
static uint16_t* tty_buffer;
static uint16_t* const VGA_MEMORY = VGA_BUFFER;

// Command buffer
#define CMD_BUFFER_SIZE 256
char cmd_buffer[CMD_BUFFER_SIZE];
int cmd_buffer_pos = 0;

static inline uint8_t vga_entry_color(enum VGA_COLOR fg, enum VGA_COLOR bg) {
    return fg | bg << 4;
}

static inline uint16_t vga_entry(unsigned char uc, uint8_t color) {
    return (uint16_t) uc | (uint16_t) color << 8;
}

void tty_init(void) {
    tty_row = 0;
    tty_column = 0;
    tty_color = vga_entry_color(PRINT_COLOR_YELLOW, PRINT_COLOR_BLACK);
    tty_buffer = VGA_MEMORY;
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            const size_t index = y * VGA_WIDTH + x;
            tty_buffer[index] = vga_entry(' ', tty_color);
        }
    }
}

void tty_clear(void) {
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            const size_t index = y * VGA_WIDTH + x;
            tty_buffer[index] = vga_entry(' ', tty_color);
        }
    }
}

void tty_setcolor(uint8_t color) {
    tty_color = color;
}

// Scroll the screen up by one line
static void tty_scroll(void) {
    // Move all lines up by one
    for (size_t y = 0; y < VGA_HEIGHT - 1; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            const size_t dst_index = y * VGA_WIDTH + x;
            const size_t src_index = (y + 1) * VGA_WIDTH + x;
            tty_buffer[dst_index] = tty_buffer[src_index];
        }
    }
    
    // Clear the last line
    for (size_t x = 0; x < VGA_WIDTH; x++) {
        const size_t index = (VGA_HEIGHT - 1) * VGA_WIDTH + x;
        tty_buffer[index] = vga_entry(' ', tty_color);
    }
}

void tty_putchar_at(unsigned char c, uint8_t color, size_t x, size_t y) {
    if (c == '\n') {
        tty_column = 0;
        tty_row++;
        set_cursor_offset((tty_row * VGA_WIDTH + tty_column));
        return;
    }
    const size_t index = y * VGA_WIDTH + x;
    tty_buffer[index] = vga_entry(c, color);
    tty_column = x + 1;
    tty_row = y;
    set_cursor_offset((tty_row * VGA_WIDTH + tty_column));
}

// Internal version for printing that doesn't add to command buffer
void tty_putchar_internal(char c) {
    if (c == '\n') {
        tty_column = 0;
        tty_row++;
        if (tty_row >= VGA_HEIGHT) {
            tty_scroll();
            tty_row = VGA_HEIGHT - 1;
        }
        set_cursor_offset(tty_row * VGA_WIDTH + tty_column);
        return;
    }
    
    unsigned char uc = c;
    tty_putchar_at(uc, tty_color, tty_column, tty_row);
    
    if (tty_column >= VGA_WIDTH) {
        tty_column = 0;
        tty_row++;
        if (tty_row >= VGA_HEIGHT) {
            tty_scroll();
            tty_row = VGA_HEIGHT - 1;
        }
        set_cursor_offset(tty_row * VGA_WIDTH + tty_column);
    }
}

// Public version - used by keyboard to echo characters
void tty_putchar(char c) {
    // Add printable characters to command buffer
    if (c != '\b' && c != '\n' && cmd_buffer_pos < CMD_BUFFER_SIZE - 1) {
        cmd_buffer[cmd_buffer_pos++] = c;
    }
    tty_putchar_internal(c);
}

void tty_putnbr(int num) {
    if (num == 0) {
        tty_putchar('0');
        return;
    }
    
    char buffer[20];
    int i = 0;
    int is_negative = 0;
    
    if (num < 0) {
        is_negative = 1;
        num = -num;
    }
    
    while (num > 0) {
        buffer[i++] = (num % 10) + '0';
        num /= 10;
    }
    
    if (is_negative) {
        buffer[i++] = '-';
    }
    
    // Print the number in reverse
    for (int j = i - 1; j >= 0; j--) {
        tty_putchar(buffer[j]);
    }
}

void tty_putstr(const char* data) {
    for (int i = 0; i < strlength(data); i++)
        tty_putchar_internal(data[i]);
}

void tty_middle_screen(const char* data) {
    size_t len = strlength(data);
    size_t x = (VGA_WIDTH - len) / 2;
    size_t y = VGA_HEIGHT / 2;
    for (int i = 0; i < len; i++)
        tty_putchar_at(data[i], tty_color, x + i, y);
}

void set_cursor_offset(size_t offset) {
    // Send the high byte of the offset
    outb(0x3D4, 14);                   // Command port for high byte
    outb(0x3D5, (uint8_t)(offset >> 8)); // Send high byte

    // Send the low byte of the offset
    outb(0x3D4, 15);                   // Command port for low byte
    outb(0x3D5, (uint8_t)(offset & 0xFF)); // Send low byte
}

// Handle backspace
void tty_backspace(void) {
    if (cmd_buffer_pos > 0) {
        cmd_buffer_pos--;
        
        // Move cursor back
        if (tty_column > 0) {
            tty_column--;
        } else if (tty_row > 0) {
            tty_row--;
            tty_column = VGA_WIDTH - 1;
        }
        
        // Clear the character
        const size_t index = tty_row * VGA_WIDTH + tty_column;
        tty_buffer[index] = vga_entry(' ', tty_color);
        set_cursor_offset(tty_row * VGA_WIDTH + tty_column);
    }
}

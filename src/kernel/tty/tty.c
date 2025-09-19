//
// Created by dan13615 on 11/15/24.
//

#include <stddef.h>
#include "string.h"
#include "tty.h"
#include "vga.h"

static size_t tty_row;
static size_t tty_column;
static uint8_t tty_color;
static uint16_t* tty_buffer;
static uint16_t* const VGA_MEMORY = VGA_BUFFER;

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

void tty_putchar_at(unsigned char c, uint8_t color, size_t x, size_t y) {
    if (c == '\n') {
        tty_column = 0;
        tty_row++;
        return;
    }
    const size_t index = y * VGA_WIDTH + x;
    tty_buffer[index] = vga_entry(c, color);
    tty_column = x + 1;
    tty_row = y;
}

void tty_putchar(char c) {
    unsigned char uc = c;
    tty_putchar_at(uc, tty_color, tty_column, tty_row);
    if (tty_column == VGA_WIDTH - 1)
        tty_column = 0;
    if (tty_row == VGA_HEIGHT - 1)
        tty_row = 0;
}

void tty_putstr(const char* data) {
    for (int i = 0; i < strlength(data); i++)
        tty_putchar(data[i]);
}

void tty_middle_screen(const char* data) {
    size_t len = strlength(data);
    size_t x = (VGA_WIDTH - len) / 2;
    size_t y = VGA_HEIGHT / 2;
    for (int i = 0; i < len; i++)
        tty_putchar_at(data[i], tty_color, x + i, y);
}

//
// Created by dan13615 on 11/15/24.
//

#ifndef PRINT_H
	#define PRINT_H

	#include <stddef.h>
	#include <stdint.h>

	void tty_init(void);
    void tty_clear(void);
	void tty_setcolor(uint8_t color);
    void tty_putchar(char c);
	void tty_putstr(const char* data);
    void tty_putchar_at(unsigned char c, uint8_t color, size_t x, size_t y);
    void tty_middle_screen(const char* data);
	void set_cursor_offset(size_t offset);
	void tty_backspace(void);
	void tty_process_command(void);
	void tty_putchar_internal(char c);

#endif //PRINT_H

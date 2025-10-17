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
	void tty_putdec(uint32_t num);
    void tty_putchar_at(unsigned char c, uint8_t color, size_t x, size_t y);
    void tty_middle_screen(const char* data);
	void set_cursor_offset(size_t offset);
	void tty_backspace(void);
	void tty_process_command(void);
	void tty_putchar_internal(char c);
	void tty_puthex64(uint64_t v);
	
	// Line editing functions
	void tty_cursor_left(void);
	void tty_cursor_right(void);
	void tty_cursor_up(void);
	void tty_cursor_down(void);
	
	// Text editor functions
	void tty_start_editor_mode(const char* filename);
	void tty_exit_editor_mode(void);
	int tty_is_editor_mode(void);
	void tty_editor_add_char(char c);
	void tty_editor_backspace(void);
	void tty_editor_redraw(void);

#endif //PRINT_H

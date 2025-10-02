
//
// Created by Shirosaaki on 02/10/2025.
//
#include "commands.h"
extern void tty_putchar_internal(char c);
extern size_t tty_row;
extern size_t tty_column;
extern uint8_t tty_color;
extern char cmd_buffer[];
extern int cmd_buffer_pos;

void tty_process_command(void) {
    cmd_buffer[cmd_buffer_pos] = '\0'; // Null terminate
    
    // Process the command
    if (cmd_buffer_pos > 0) {
        if (strncmp(cmd_buffer, "help", 4) == 0) {
            tty_putstr("Available commands:\n");
            tty_putstr("  help   - Show this help message\n");
            tty_putstr("  clear  - Clear the screen\n");
            tty_putstr("  echo   - Echo back the input\n");
            tty_putstr("  about  - Show OS information\n");
        } else if (strncmp(cmd_buffer, "cls", 3) == 0) {
            tty_clear();
            tty_row = 0;
            tty_column = 0;
        } else if (strncmp(cmd_buffer, "about", 5) == 0) {
            tty_putstr("DanOS - A simple 64-bit operating system\n");
            tty_putstr("Version: 0.1\n");
            tty_putstr("Author: dan13615\n");
        } else if (strncmp(cmd_buffer, "echo ", 5) == 0) {
            for (int i = 5; i < strlength(cmd_buffer); i++) {
                tty_putchar_internal(cmd_buffer[i]);
            }
            tty_putchar_internal('\n');
        } else {
            tty_putstr("Unknown command: ");
            tty_putstr(cmd_buffer);
            tty_putstr("\n");
            tty_putstr("Type 'help' for available commands.\n");
        }
    }
    
    // Reset command buffer
    cmd_buffer_pos = 0;
    tty_putstr("> ");
}

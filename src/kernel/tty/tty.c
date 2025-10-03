//
// Created by dan13615 on 11/15/24.
//

#include <stddef.h>
#include "string.h"
#include "tty.h"
#include "vga.h"
#include "../../cpu/ports.h"
#include "fat32.h"

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
int cmd_cursor_pos = 0; // Cursor position in command buffer (non-static for commands.c)
static size_t prompt_column = 0; // Column where prompt ends

// Editor mode variables
static int editor_mode = 0;
static char editor_filename[64];
#define EDITOR_BUFFER_SIZE 2048
static char editor_buffer[EDITOR_BUFFER_SIZE];
static int editor_buffer_pos = 0; // Total characters in buffer
static int editor_cursor_pos = 0; // Cursor position in buffer (0 to editor_buffer_pos)
static size_t editor_content_start_row = 0; // Row where editor content starts (after header)
static size_t editor_content_start_col = 0; // Column where editor content starts

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
        cmd_cursor_pos = cmd_buffer_pos; // Keep cursor at end
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

// Print decimal number (non-interactive, for system output)
void tty_putdec(uint32_t num) {
    if (num == 0) {
        tty_putchar_internal('0');
        return;
    }
    
    char buffer[20];
    int i = 0;
    
    while (num > 0) {
        buffer[i++] = (num % 10) + '0';
        num /= 10;
    }
    
    // Print the number in reverse
    for (int j = i - 1; j >= 0; j--) {
        tty_putchar_internal(buffer[j]);
    }
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
    // Check if in editor mode or normal command mode
    if (editor_mode) {
        // In editor mode - always allow backspace for visual feedback
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
    } else {
        // Normal command mode
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
}

// Helper: Calculate screen position from buffer position
static void editor_calculate_screen_pos(int buffer_pos, size_t* out_row, size_t* out_col) {
    *out_row = editor_content_start_row;
    *out_col = editor_content_start_col;
    
    for (int i = 0; i < buffer_pos && i < editor_buffer_pos; i++) {
        if (editor_buffer[i] == '\n') {
            (*out_row)++;
            *out_col = 0;
        } else {
            (*out_col)++;
            if (*out_col >= VGA_WIDTH) {
                (*out_row)++;
                *out_col = 0;
            }
        }
    }
}

// Helper: Redraw all editor content
static void editor_full_redraw(void) {
    // Clear screen from editor start
    for (size_t y = editor_content_start_row; y < VGA_HEIGHT; y++) {
        for (size_t x = (y == editor_content_start_row ? editor_content_start_col : 0); x < VGA_WIDTH; x++) {
            const size_t index = y * VGA_WIDTH + x;
            tty_buffer[index] = vga_entry(' ', tty_color);
        }
    }
    
    // Redraw all content
    tty_row = editor_content_start_row;
    tty_column = editor_content_start_col;
    
    for (int i = 0; i < editor_buffer_pos; i++) {
        if (editor_buffer[i] == '\n') {
            // Move to next line
            tty_row++;
            tty_column = 0;
        } else {
            // Put character
            const size_t index = tty_row * VGA_WIDTH + tty_column;
            tty_buffer[index] = vga_entry(editor_buffer[i], tty_color);
            tty_column++;
            if (tty_column >= VGA_WIDTH) {
                tty_column = 0;
                tty_row++;
            }
        }
    }
    
    // Position cursor at editor_cursor_pos
    editor_calculate_screen_pos(editor_cursor_pos, &tty_row, &tty_column);
    set_cursor_offset(tty_row * VGA_WIDTH + tty_column);
}

// Start editor mode
void tty_start_editor_mode(const char* filename) {
    editor_mode = 1;
    editor_buffer_pos = 0;
    editor_cursor_pos = 0;
    
    // Clear buffer
    for (int i = 0; i < EDITOR_BUFFER_SIZE; i++) {
        editor_buffer[i] = '\0';
    }
    
    // Copy filename
    int i;
    for (i = 0; i < 63 && filename[i] != '\0'; i++) {
        editor_filename[i] = filename[i];
    }
    editor_filename[i] = '\0';
    
    // Clear screen
    tty_clear();
    tty_row = 0;
    tty_column = 0;
    
    tty_putstr("=== Text Editor ===\n");
    tty_putstr("File: ");
    tty_putstr(editor_filename);
    tty_putstr("\nPress Ctrl to save and exit\n");
    tty_putstr("Use arrow keys to move cursor\n");
    tty_putstr("-------------------\n");
    
    // Remember where editor content starts
    editor_content_start_row = tty_row;
    editor_content_start_col = tty_column;
    
    // Try to load existing file
    fat32_file_t file;
    if (fat32_open_file(editor_filename, &file) == 0) {
        uint32_t bytes_to_read = file.file_size > EDITOR_BUFFER_SIZE - 1 ? EDITOR_BUFFER_SIZE - 1 : file.file_size;
        int bytes_read = fat32_read_file(&file, (uint8_t*)editor_buffer, bytes_to_read);
        
        if (bytes_read > 0) {
            editor_buffer_pos = bytes_read;
            editor_cursor_pos = 0; // Start cursor at beginning
            editor_buffer[editor_buffer_pos] = '\0';
        }
    }
    
    // Draw initial content
    editor_full_redraw();
}

// Exit editor mode and save file
void tty_exit_editor_mode(void) {
    if (!editor_mode) return;
    
    editor_mode = 0;
    
    // Clear screen and return to normal mode
    tty_clear();
    tty_row = 0;
    tty_column = 0;
    
    // Null terminate the editor buffer
    if (editor_buffer_pos < EDITOR_BUFFER_SIZE) {
        editor_buffer[editor_buffer_pos] = '\0';
    }
    
    tty_putstr("Saving file: ");
    tty_putstr(editor_filename);
    tty_putstr(" (");
    // Simple number display for buffer size
    if (editor_buffer_pos == 0) tty_putstr("0");
    else if (editor_buffer_pos < 10) {
        char num = '0' + editor_buffer_pos;
        tty_putchar_internal(num);
    } else {
        tty_putstr("many");
    }
    tty_putstr(" bytes)\n");
    
    // Save file using FAT32 with dynamic sizing
    int result = fat32_update_file(editor_filename, (uint8_t*)editor_buffer, editor_buffer_pos);
    
    if (result == 0) {
        tty_putstr("File saved successfully: ");
        tty_putstr(editor_filename);
        tty_putstr("\n");
    } else {
        tty_putstr("Error: Could not save file ");
        tty_putstr(editor_filename);
        tty_putstr("\n");
        tty_putstr("Check if FAT32 is initialized and disk is available\n");
    }
    cmd_buffer_pos = 0; // Clear command buffer
    cmd_buffer[0] = '\0';
    
    // Show current directory in prompt
    char path[64];
    fat32_get_current_path(path, 64);
    tty_putstr("\nDanOS:");
    tty_putstr(path);
    tty_putstr("$ ");
}

// Check if in editor mode
int tty_is_editor_mode(void) {
    return editor_mode;
}

// Add character to editor buffer at cursor position
void tty_editor_add_char(char c) {
    if (editor_buffer_pos >= EDITOR_BUFFER_SIZE - 1) {
        return; // Buffer full
    }
    
    // Shift characters right if inserting in middle
    if (editor_cursor_pos < editor_buffer_pos) {
        for (int i = editor_buffer_pos; i > editor_cursor_pos; i--) {
            editor_buffer[i] = editor_buffer[i - 1];
        }
    }
    
    // Insert character
    editor_buffer[editor_cursor_pos] = c;
    editor_cursor_pos++;
    editor_buffer_pos++;
    
    // Redraw from cursor position
    editor_full_redraw();
}

// Handle backspace in editor - remove character before cursor
void tty_editor_backspace(void) {
    if (editor_cursor_pos <= 0) {
        return; // Nothing to delete
    }
    
    // Shift characters left
    for (int i = editor_cursor_pos - 1; i < editor_buffer_pos - 1; i++) {
        editor_buffer[i] = editor_buffer[i + 1];
    }
    
    editor_cursor_pos--;
    editor_buffer_pos--;
    editor_buffer[editor_buffer_pos] = '\0';
    
    // Redraw
    editor_full_redraw();
}

// Redraw editor content (kept for compatibility)
void tty_editor_redraw(void) {
    editor_full_redraw();
}

// Cursor movement functions for line editing
void tty_cursor_left(void) {
    if (editor_mode) {
        // In editor mode - move cursor left in buffer
        if (editor_cursor_pos > 0) {
            editor_cursor_pos--;
            // Update screen cursor
            editor_calculate_screen_pos(editor_cursor_pos, &tty_row, &tty_column);
            set_cursor_offset(tty_row * VGA_WIDTH + tty_column);
        }
    } else {
        // In command mode - respect command buffer boundaries
        if (cmd_cursor_pos > 0) {
            cmd_cursor_pos--;
            if (tty_column > 0) {
                tty_column--;
                set_cursor_offset(tty_row * VGA_WIDTH + tty_column);
            }
        }
    }
}

void tty_cursor_right(void) {
    if (editor_mode) {
        // In editor mode - can't move past end of buffer content
        if (editor_cursor_pos < editor_buffer_pos) {
            editor_cursor_pos++;
            // Update screen cursor
            editor_calculate_screen_pos(editor_cursor_pos, &tty_row, &tty_column);
            set_cursor_offset(tty_row * VGA_WIDTH + tty_column);
        }
    } else {
        // In command mode - can't move past end of typed text
        if (cmd_cursor_pos < cmd_buffer_pos) {
            cmd_cursor_pos++;
            if (tty_column < VGA_WIDTH - 1) {
                tty_column++;
                set_cursor_offset(tty_row * VGA_WIDTH + tty_column);
            }
        }
    }
}

void tty_cursor_up(void) {
    if (editor_mode) {
        // Find start of current line
        int line_start = editor_cursor_pos;
        while (line_start > 0 && editor_buffer[line_start - 1] != '\n') {
            line_start--;
        }
        
        // If we're not on first line
        if (line_start > 0) {
            // Find start of previous line
            int prev_line_start = line_start - 2; // Skip the \n
            while (prev_line_start > 0 && editor_buffer[prev_line_start - 1] != '\n') {
                prev_line_start--;
            }
            
            // Calculate column offset in current line
            int col_offset = editor_cursor_pos - line_start;
            
            // Find length of previous line
            int prev_line_len = (line_start - 1) - prev_line_start;
            
            // Move to same column in previous line, or end if shorter
            if (col_offset > prev_line_len) {
                editor_cursor_pos = line_start - 1; // End of previous line
            } else {
                editor_cursor_pos = prev_line_start + col_offset;
            }
            
            // Update screen cursor
            editor_calculate_screen_pos(editor_cursor_pos, &tty_row, &tty_column);
            set_cursor_offset(tty_row * VGA_WIDTH + tty_column);
        }
    } else {
        // Command mode - TODO: command history
        if (tty_row > 0) {
            tty_row--;
            set_cursor_offset(tty_row * VGA_WIDTH + tty_column);
        }
    }
}

void tty_cursor_down(void) {
    if (editor_mode) {
        // Find start of current line
        int line_start = editor_cursor_pos;
        while (line_start > 0 && editor_buffer[line_start - 1] != '\n') {
            line_start--;
        }
        
        // Find end of current line
        int line_end = editor_cursor_pos;
        while (line_end < editor_buffer_pos && editor_buffer[line_end] != '\n') {
            line_end++;
        }
        
        // If there's a next line
        if (line_end < editor_buffer_pos) {
            int next_line_start = line_end + 1; // After the \n
            
            // Calculate column offset in current line
            int col_offset = editor_cursor_pos - line_start;
            
            // Find end of next line
            int next_line_end = next_line_start;
            while (next_line_end < editor_buffer_pos && editor_buffer[next_line_end] != '\n') {
                next_line_end++;
            }
            
            int next_line_len = next_line_end - next_line_start;
            
            // Move to same column in next line, or end if shorter
            if (col_offset > next_line_len) {
                editor_cursor_pos = next_line_end;
            } else {
                editor_cursor_pos = next_line_start + col_offset;
            }
            
            // Update screen cursor
            editor_calculate_screen_pos(editor_cursor_pos, &tty_row, &tty_column);
            set_cursor_offset(tty_row * VGA_WIDTH + tty_column);
        }
    } else {
        // Command mode - TODO: command history
        if (tty_row < VGA_HEIGHT - 1) {
            tty_row++;
            set_cursor_offset(tty_row * VGA_WIDTH + tty_column);
        }
    }
}

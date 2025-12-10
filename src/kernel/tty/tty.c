//
// Created by dan13615 on 11/15/24.
//

#include <stddef.h>
#include "string.h"
#include "tty.h"
#include "vga.h"
#include "../../cpu/ports.h"
#include "fat32.h"
#include "rtc.h"
#include "framebuffer.h"

// Make these non-static so they can be accessed from commands.c
size_t tty_row;
size_t tty_column;
uint8_t tty_color;
static uint16_t* tty_buffer;
static uint16_t* const VGA_MEMORY = VGA_BUFFER;

// Dynamic screen dimensions (VGA or framebuffer based)
static size_t screen_width = VGA_WIDTH;
static size_t screen_height = VGA_HEIGHT;

// Helper: Convert VGA color to 32-bit RGB for framebuffer
static uint32_t vga_to_rgb(uint8_t vga_color) {
    // Map VGA 4-bit colors to 32-bit RGB
    static const uint32_t vga_palette[16] = {
        0x00000000,  // 0: Black
        0x000000AA,  // 1: Blue
        0x0000AA00,  // 2: Green
        0x0000AAAA,  // 3: Cyan
        0x00AA0000,  // 4: Red
        0x00AA00AA,  // 5: Magenta
        0x00AA5500,  // 6: Brown
        0x00AAAAAA,  // 7: Light Grey
        0x00555555,  // 8: Dark Grey
        0x005555FF,  // 9: Light Blue
        0x0055FF55,  // 10: Light Green
        0x0055FFFF,  // 11: Light Cyan
        0x00FF5555,  // 12: Light Red
        0x00FF55FF,  // 13: Pink
        0x00FFFF55,  // 14: Yellow
        0x00FFFFFF   // 15: White
    };
    return vga_palette[vga_color & 0x0F];
}

// Command buffer
#define CMD_BUFFER_SIZE 256
char cmd_buffer[CMD_BUFFER_SIZE];
int cmd_buffer_pos = 0;
int cmd_cursor_pos = 0; // Cursor position in command buffer (non-static for commands.c)
static size_t prompt_column = 0; // Column where prompt ends
static size_t prompt_row = 0;    // Row where prompt starts

// Command history
#define HISTORY_SIZE 16
#define HISTORY_CMD_SIZE 256
static char history[HISTORY_SIZE][HISTORY_CMD_SIZE];
static int history_count = 0;      // Total commands in history
static int history_index = -1;     // Current position when navigating (-1 = current input)
static char temp_cmd_buffer[CMD_BUFFER_SIZE]; // Store current input when navigating history
static int temp_cmd_saved = 0;     // Flag: have we saved current input?

// Stop signal flag for Ctrl+D
static volatile int stop_signal = 0;

// Clipboard and selection for copy/paste
#define CLIPBOARD_SIZE 256
static char clipboard[CLIPBOARD_SIZE];
static int clipboard_len = 0;
static int selection_active = 0;      // Is selection mode active?
static int selection_start = -1;      // Start position in cmd_buffer
static int selection_end = -1;        // End position in cmd_buffer

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
    
    // Check if framebuffer is available AND properly initialized
    if (fb_is_available() && terminal_get_cols() > 0 && terminal_get_rows() > 0) {
        // Use framebuffer dimensions
        screen_width = terminal_get_cols();
        screen_height = terminal_get_rows();
        // Set matching colors in terminal
        terminal_set_colors(vga_to_rgb(tty_color & 0x0F), 
                           vga_to_rgb((tty_color >> 4) & 0x0F));
    } else {
        // Use VGA text mode (default fallback)
        screen_width = VGA_WIDTH;
        screen_height = VGA_HEIGHT;
        tty_buffer = VGA_MEMORY;
        for (size_t y = 0; y < screen_height; y++) {
            for (size_t x = 0; x < screen_width; x++) {
                const size_t index = y * screen_width + x;
                tty_buffer[index] = vga_entry(' ', tty_color);
            }
        }
    }
}

void tty_clear(void) {
    if (fb_is_available()) {
        terminal_clear();
    } else {
        for (size_t y = 0; y < screen_height; y++) {
            for (size_t x = 0; x < screen_width; x++) {
                const size_t index = y * screen_width + x;
                tty_buffer[index] = vga_entry(' ', tty_color);
            }
        }
        set_cursor_offset(0);
    }
    tty_row = 0;
    tty_column = 0;
}

void tty_setcolor(uint8_t color) {
    tty_color = color;
    if (fb_is_available()) {
        terminal_set_colors(vga_to_rgb(color & 0x0F), 
                           vga_to_rgb((color >> 4) & 0x0F));
    }
}

// Scroll the screen up by one line
static void tty_scroll(void) {
    if (fb_is_available()) {
        terminal_scroll();
    } else {
        // Move all lines up by one (VGA mode)
        for (size_t y = 0; y < screen_height - 1; y++) {
            for (size_t x = 0; x < screen_width; x++) {
                const size_t dst_index = y * screen_width + x;
                const size_t src_index = (y + 1) * screen_width + x;
                tty_buffer[dst_index] = tty_buffer[src_index];
            }
        }
        
        // Clear the last line
        for (size_t x = 0; x < screen_width; x++) {
            const size_t index = (screen_height - 1) * screen_width + x;
            tty_buffer[index] = vga_entry(' ', tty_color);
        }
    }
}

void tty_putchar_at(unsigned char c, uint8_t color, size_t x, size_t y) {
    if (c == '\n') {
        tty_column = 0;
        tty_row++;
        // Don't draw cursor here - caller will handle it
        return;
    }
    
    if (fb_is_available()) {
        terminal_draw_char_at(c, x, y, 
                              vga_to_rgb(color & 0x0F),
                              vga_to_rgb((color >> 4) & 0x0F));
    } else {
        const size_t index = y * screen_width + x;
        tty_buffer[index] = vga_entry(c, color);
    }
    tty_column = x + 1;
    tty_row = y;
    // Don't draw cursor here - caller will handle it
}

// Internal version for printing that doesn't add to command buffer
void tty_putchar_internal(char c) {
    if (c == '\n') {
        tty_column = 0;
        tty_row++;
        if (tty_row >= screen_height) {
            tty_scroll();
            tty_row = screen_height - 1;
        }
        // Don't update cursor here - batch output will handle it
        return;
    }
    
    unsigned char uc = c;
    tty_putchar_at(uc, tty_color, tty_column, tty_row);
    
    if (tty_column >= screen_width) {
        tty_column = 0;
        tty_row++;
        if (tty_row >= screen_height) {
            tty_scroll();
            tty_row = screen_height - 1;
        }
    }
}

// Public version - used by keyboard to echo characters
// Uses insert mode: typing inserts character at cursor and shifts rest right
void tty_putchar(char c) {
    // Add printable characters to command buffer
    if (c != '\b' && c != '\n' && cmd_buffer_pos < CMD_BUFFER_SIZE - 1) {
        if (cmd_cursor_pos < cmd_buffer_pos) {
            // Insert mode: shift characters right and insert at cursor position
            for (int i = cmd_buffer_pos; i > cmd_cursor_pos; i--) {
                cmd_buffer[i] = cmd_buffer[i - 1];
            }
            cmd_buffer[cmd_cursor_pos] = c;
            cmd_buffer_pos++;
            cmd_cursor_pos++;
            
            // Redraw from cursor position to end
            size_t saved_row = tty_row;
            size_t saved_col = tty_column;
            
            // Draw the inserted char and all following chars
            for (int i = cmd_cursor_pos - 1; i < cmd_buffer_pos; i++) {
                if (fb_is_available()) {
                    terminal_draw_char_at(cmd_buffer[i], tty_column, tty_row,
                                          vga_to_rgb(tty_color & 0x0F),
                                          vga_to_rgb((tty_color >> 4) & 0x0F));
                } else {
                    const size_t index = tty_row * screen_width + tty_column;
                    tty_buffer[index] = vga_entry(cmd_buffer[i], tty_color);
                }
                tty_column++;
                if (tty_column >= screen_width) {
                    tty_column = 0;
                    tty_row++;
                }
            }
            
            // Move cursor to correct position (after inserted char)
            tty_row = saved_row;
            tty_column = saved_col + 1;
            if (tty_column >= screen_width) {
                tty_column = 0;
                tty_row++;
            }
            set_cursor_offset(tty_row * screen_width + tty_column);
        } else {
            // At end of buffer: just append normally
            cmd_buffer[cmd_buffer_pos++] = c;
            cmd_cursor_pos++;
            tty_putchar_internal(c);
        }
    } else if (c == '\n') {
        tty_putchar_internal(c);
    }
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
    // Hide cursor during output
    if (fb_is_available()) {
        terminal_hide_cursor();
    }
    
    for (int i = 0; i < strlength(data); i++)
        tty_putchar_internal(data[i]);
    
    // Update cursor position and show cursor
    if (fb_is_available()) {
        terminal_set_cursor(tty_column, tty_row);
        terminal_draw_cursor();
    } else {
        set_cursor_offset(tty_row * screen_width + tty_column);
    }
}

// Print decimal number (non-interactive, for system output)
void tty_putdec(uint32_t num) {
    // Hide cursor during output
    if (fb_is_available()) {
        terminal_hide_cursor();
    }
    
    if (num == 0) {
        tty_putchar_internal('0');
    } else {
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
    
    // Update cursor position and show cursor
    if (fb_is_available()) {
        terminal_set_cursor(tty_column, tty_row);
        terminal_draw_cursor();
    } else {
        set_cursor_offset(tty_row * screen_width + tty_column);
    }
}

void tty_puthex64(uint64_t v) {
    char buf[17];
    const char *hex = "0123456789ABCDEF";
    for (int i = 0; i < 16; ++i) {
        buf[15 - i] = hex[v & 0xF];
        v >>= 4;
    }
    buf[16] = '\0';
    tty_putstr("0x");
    tty_putstr(buf);
}

void tty_middle_screen(const char* data) {
    size_t len = strlength(data);
    size_t x = (screen_width - len) / 2;
    size_t y = screen_height / 2;
    for (int i = 0; i < len; i++)
        tty_putchar_at(data[i], tty_color, x + i, y);
}

void set_cursor_offset(size_t offset) {
    if (fb_is_available()) {
        // In framebuffer mode, update position and draw cursor
        terminal_hide_cursor();
        size_t col = offset % screen_width;
        size_t row = offset / screen_width;
        terminal_set_cursor(col, row);
        terminal_draw_cursor();
        return;
    }
    
    // VGA hardware cursor (only works in text mode)
    outb(0x3D4, 14);
    outb(0x3D5, (uint8_t)(offset >> 8));
    outb(0x3D4, 15);
    outb(0x3D5, (uint8_t)(offset & 0xFF));
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
            tty_column = screen_width - 1;
        }
        
        // Clear the character
        if (fb_is_available()) {
            terminal_draw_char_at(' ', tty_column, tty_row,
                                  vga_to_rgb(tty_color & 0x0F),
                                  vga_to_rgb((tty_color >> 4) & 0x0F));
        } else {
            const size_t index = tty_row * screen_width + tty_column;
            tty_buffer[index] = vga_entry(' ', tty_color);
        }
        set_cursor_offset(tty_row * screen_width + tty_column);
    } else {
        // Normal command mode with cursor support
        if (cmd_cursor_pos > 0) {
            // Delete character before cursor position
            cmd_cursor_pos--;
            
            // Shift all characters after cursor left by one
            for (int i = cmd_cursor_pos; i < cmd_buffer_pos - 1; i++) {
                cmd_buffer[i] = cmd_buffer[i + 1];
            }
            cmd_buffer_pos--;
            
            // Move screen cursor back
            if (tty_column > 0) {
                tty_column--;
            } else if (tty_row > 0) {
                tty_row--;
                tty_column = screen_width - 1;
            }
            
            // Redraw from cursor position to end of command
            size_t saved_row = tty_row;
            size_t saved_col = tty_column;
            
            // Redraw remaining characters
            for (int i = cmd_cursor_pos; i < cmd_buffer_pos; i++) {
                if (fb_is_available()) {
                    terminal_draw_char_at(cmd_buffer[i], tty_column, tty_row,
                                          vga_to_rgb(tty_color & 0x0F),
                                          vga_to_rgb((tty_color >> 4) & 0x0F));
                } else {
                    const size_t index = tty_row * screen_width + tty_column;
                    tty_buffer[index] = vga_entry(cmd_buffer[i], tty_color);
                }
                tty_column++;
                if (tty_column >= screen_width) {
                    tty_column = 0;
                    tty_row++;
                }
            }
            
            // Clear the last character position (which is now empty)
            if (fb_is_available()) {
                terminal_draw_char_at(' ', tty_column, tty_row,
                                      vga_to_rgb(tty_color & 0x0F),
                                      vga_to_rgb((tty_color >> 4) & 0x0F));
            } else {
                const size_t index = tty_row * screen_width + tty_column;
                tty_buffer[index] = vga_entry(' ', tty_color);
            }
            
            // Restore cursor position
            tty_row = saved_row;
            tty_column = saved_col;
            set_cursor_offset(tty_row * screen_width + tty_column);
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
            if (*out_col >= screen_width) {
                (*out_row)++;
                *out_col = 0;
            }
        }
    }
}

// Helper: Redraw all editor content with selection highlighting
static void editor_full_redraw(void) {
    // Clear screen from editor start
    for (size_t y = editor_content_start_row; y < screen_height; y++) {
        for (size_t x = (y == editor_content_start_row ? editor_content_start_col : 0); x < screen_width; x++) {
            if (fb_is_available()) {
                terminal_draw_char_at(' ', x, y,
                                      vga_to_rgb(tty_color & 0x0F),
                                      vga_to_rgb((tty_color >> 4) & 0x0F));
            } else {
                const size_t index = y * screen_width + x;
                tty_buffer[index] = vga_entry(' ', tty_color);
            }
        }
    }
    
    // Calculate selection bounds
    int sel_min = -1, sel_max = -1;
    if (selection_active) {
        sel_min = selection_start < selection_end ? selection_start : selection_end;
        sel_max = selection_start < selection_end ? selection_end : selection_start;
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
            // Determine color (highlighted if in selection)
            uint8_t color = tty_color;
            if (selection_active && i >= sel_min && i < sel_max) {
                color = vga_entry_color(PRINT_COLOR_BLACK, PRINT_COLOR_YELLOW);
            }
            
            // Put character
            if (fb_is_available()) {
                terminal_draw_char_at(editor_buffer[i], tty_column, tty_row,
                                      vga_to_rgb(color & 0x0F),
                                      vga_to_rgb((color >> 4) & 0x0F));
            } else {
                const size_t index = tty_row * screen_width + tty_column;
                tty_buffer[index] = vga_entry(editor_buffer[i], color);
            }
            tty_column++;
            if (tty_column >= screen_width) {
                tty_column = 0;
                tty_row++;
            }
        }
    }
    
    // Position cursor at editor_cursor_pos
    editor_calculate_screen_pos(editor_cursor_pos, &tty_row, &tty_column);
    set_cursor_offset(tty_row * screen_width + tty_column);
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
    tty_putstr("\nCtrl+S: Save | Ctrl+E: Exit\n");
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

// Exit editor mode without saving
void tty_exit_editor_mode(void) {
    if (!editor_mode) return;
    
    editor_mode = 0;
    
    // Clear screen and return to normal mode
    tty_clear();
    tty_row = 0;
    tty_column = 0;
    
    tty_putstr("Editor closed\n");
    
    cmd_buffer_pos = 0; // Clear command buffer
    cmd_cursor_pos = 0;
    cmd_buffer[0] = '\0';
    
    // Show current directory in prompt
    char path[64];
    fat32_get_current_path(path, 64);
    tty_putstr("DanOS:");
    tty_putstr(path);
    tty_putstr("$ ");
    
    // Update prompt position
    prompt_row = tty_row;
    prompt_column = tty_column;
}

// Save file in editor mode (Ctrl+S)
void tty_editor_save(void) {
    if (!editor_mode) return;
    
    // Null terminate the editor buffer
    if (editor_buffer_pos < EDITOR_BUFFER_SIZE) {
        editor_buffer[editor_buffer_pos] = '\0';
    }
    
    // Save file using FAT32 with dynamic sizing
    int result = fat32_update_file(editor_filename, (uint8_t*)editor_buffer, editor_buffer_pos);
    
    // Show save status in editor (temporarily at bottom)
    size_t saved_row = tty_row;
    size_t saved_col = tty_column;
    
    // Go to line 2 to show status
    tty_row = 2;
    tty_column = 0;
    
    // Clear the status line
    for (size_t x = 0; x < screen_width; x++) {
        if (fb_is_available()) {
            terminal_draw_char_at(' ', x, tty_row,
                                  vga_to_rgb(tty_color & 0x0F),
                                  vga_to_rgb((tty_color >> 4) & 0x0F));
        } else {
            const size_t index = tty_row * screen_width + x;
            tty_buffer[index] = vga_entry(' ', tty_color);
        }
    }
    
    if (result == 0) {
        tty_putstr("Ctrl+S: Save | Ctrl+E: Exit  [SAVED]");
    } else {
        tty_putstr("Ctrl+S: Save | Ctrl+E: Exit  [ERROR]");
    }
    
    // Restore cursor position
    tty_row = saved_row;
    tty_column = saved_col;
    set_cursor_offset(tty_row * screen_width + tty_column);
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
            set_cursor_offset(tty_row * screen_width + tty_column);
            
            // Update selection if active
            if (selection_active) {
                tty_update_selection();
            }
        }
    } else {
        // In command mode - respect command buffer boundaries
        if (cmd_cursor_pos > 0) {
            cmd_cursor_pos--;
            if (tty_column > 0) {
                tty_column--;
            } else if (tty_row > prompt_row) {
                tty_row--;
                tty_column = screen_width - 1;
            }
            set_cursor_offset(tty_row * screen_width + tty_column);
            
            // Update selection if active
            if (selection_active) {
                tty_update_selection();
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
            set_cursor_offset(tty_row * screen_width + tty_column);
            
            // Update selection if active
            if (selection_active) {
                tty_update_selection();
            }
        }
    } else {
        // In command mode - can't move past end of typed text
        if (cmd_cursor_pos < cmd_buffer_pos) {
            cmd_cursor_pos++;
            tty_column++;
            if (tty_column >= screen_width) {
                tty_column = 0;
                tty_row++;
            }
            set_cursor_offset(tty_row * screen_width + tty_column);
            
            // Update selection if active
            if (selection_active) {
                tty_update_selection();
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
            set_cursor_offset(tty_row * screen_width + tty_column);
            
            // Update selection if active
            if (selection_active) {
                tty_update_selection();
            }
        }
    } else {
        // Command mode - TODO: command history
        if (tty_row > 0) {
            tty_row--;
            set_cursor_offset(tty_row * screen_width + tty_column);
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
            set_cursor_offset(tty_row * screen_width + tty_column);
            
            // Update selection if active
            if (selection_active) {
                tty_update_selection();
            }
        }
    }
    // In command mode, up/down is handled by tty_history_up/down
}

// Add command to history (memory and disk with timestamp)
static void history_add(const char* cmd) {
    if (cmd[0] == '\0') return; // Don't add empty commands
    
    // Don't add if same as last command
    if (history_count > 0) {
        int last_idx = (history_count - 1) % HISTORY_SIZE;
        int same = 1;
        for (int i = 0; cmd[i] != '\0' || history[last_idx][i] != '\0'; i++) {
            if (cmd[i] != history[last_idx][i]) {
                same = 0;
                break;
            }
        }
        if (same) return;
    }
    
    // Add to in-memory history (circular buffer)
    int idx = history_count % HISTORY_SIZE;
    int i;
    for (i = 0; i < HISTORY_CMD_SIZE - 1 && cmd[i] != '\0'; i++) {
        history[idx][i] = cmd[i];
    }
    history[idx][i] = '\0';
    history_count++;
    
    // Save to .history file on disk with timestamp
    // Format: [YYYY-MM-DD HH:MM:SS] command\n
    rtc_time_t time;
    rtc_read_local_time(&time);  // Use local time with timezone
    
    // Build the history entry
    char entry[320];
    int pos = 0;
    
    // Add timestamp: [YYYY-MM-DD HH:MM:SS]
    entry[pos++] = '[';
    
    // Year
    entry[pos++] = '0' + (time.year / 1000) % 10;
    entry[pos++] = '0' + (time.year / 100) % 10;
    entry[pos++] = '0' + (time.year / 10) % 10;
    entry[pos++] = '0' + time.year % 10;
    entry[pos++] = '-';
    
    // Month
    entry[pos++] = '0' + (time.month / 10) % 10;
    entry[pos++] = '0' + time.month % 10;
    entry[pos++] = '-';
    
    // Day
    entry[pos++] = '0' + (time.day / 10) % 10;
    entry[pos++] = '0' + time.day % 10;
    entry[pos++] = ' ';
    
    // Hours
    entry[pos++] = '0' + (time.hours / 10) % 10;
    entry[pos++] = '0' + time.hours % 10;
    entry[pos++] = ':';
    
    // Minutes
    entry[pos++] = '0' + (time.minutes / 10) % 10;
    entry[pos++] = '0' + time.minutes % 10;
    entry[pos++] = ':';
    
    // Seconds
    entry[pos++] = '0' + (time.seconds / 10) % 10;
    entry[pos++] = '0' + time.seconds % 10;
    entry[pos++] = ']';
    entry[pos++] = ' ';
    
    // Add the command
    for (i = 0; cmd[i] != '\0' && pos < 318; i++) {
        entry[pos++] = cmd[i];
    }
    entry[pos++] = '\n';
    entry[pos] = '\0';
    
    // Append to .history file
    fat32_file_t file;
    if (fat32_open_file(".history", &file) == 0) {
        // File exists - append to it
        // Read existing content
        uint8_t existing[4096];
        int existing_len = 0;
        if (file.file_size < 4096 - 320) {
            existing_len = fat32_read_file(&file, existing, file.file_size);
            if (existing_len < 0) existing_len = 0;
        } else {
            // File too large, read last part only (keep ~3700 bytes)
            // Skip to near end
            int skip = file.file_size - 3700;
            if (skip < 0) skip = 0;
            existing_len = fat32_read_file(&file, existing, file.file_size);
            if (existing_len > 3700) {
                // Find a newline to start from
                int start = existing_len - 3700;
                while (start < existing_len && existing[start] != '\n') start++;
                if (start < existing_len) start++; // Skip the newline
                // Shift content
                int new_len = 0;
                for (int j = start; j < existing_len; j++) {
                    existing[new_len++] = existing[j];
                }
                existing_len = new_len;
            }
        }
        
        // Append new entry
        for (i = 0; entry[i] != '\0' && existing_len < 4095; i++) {
            existing[existing_len++] = entry[i];
        }
        existing[existing_len] = '\0';
        
        // Write back
        fat32_update_file(".history", existing, existing_len);
    } else {
        // File doesn't exist - create it
        fat32_create_file(".history", (uint8_t*)entry, pos);
    }
}

// Helper: Clear current command line on screen
static void clear_command_line(void) {
    // Hide cursor during clearing
    if (fb_is_available()) {
        terminal_hide_cursor();
    }
    
    // Move to prompt position and clear the line
    tty_row = prompt_row;
    tty_column = prompt_column;
    
    // Clear from prompt to end of line
    for (size_t x = prompt_column; x < screen_width; x++) {
        if (fb_is_available()) {
            terminal_draw_char_at(' ', x, tty_row,
                                  vga_to_rgb(tty_color & 0x0F),
                                  vga_to_rgb((tty_color >> 4) & 0x0F));
        } else {
            const size_t index = tty_row * screen_width + x;
            tty_buffer[index] = vga_entry(' ', tty_color);
        }
    }
    
    // Update cursor
    if (fb_is_available()) {
        terminal_set_cursor(tty_column, tty_row);
        terminal_draw_cursor();
    } else {
        set_cursor_offset(tty_row * screen_width + tty_column);
    }
}

// Helper: Display a command on the current line
static void display_command(const char* cmd) {
    clear_command_line();
    
    // Hide cursor while displaying
    if (fb_is_available()) {
        terminal_hide_cursor();
    }
    
    // Copy to cmd_buffer and display
    cmd_buffer_pos = 0;
    for (int i = 0; cmd[i] != '\0' && cmd_buffer_pos < CMD_BUFFER_SIZE - 1; i++) {
        cmd_buffer[cmd_buffer_pos++] = cmd[i];
        tty_putchar_internal(cmd[i]);
    }
    cmd_cursor_pos = cmd_buffer_pos;
    
    // Show cursor at end
    if (fb_is_available()) {
        terminal_set_cursor(tty_column, tty_row);
        terminal_draw_cursor();
    } else {
        set_cursor_offset(tty_row * screen_width + tty_column);
    }
}

// Navigate to previous command in history (up arrow)
void tty_history_up(void) {
    if (history_count == 0) return;
    
    // Save current input if we're just starting to navigate
    if (history_index == -1) {
        temp_cmd_saved = 1;
        for (int i = 0; i < cmd_buffer_pos && i < CMD_BUFFER_SIZE - 1; i++) {
            temp_cmd_buffer[i] = cmd_buffer[i];
        }
        temp_cmd_buffer[cmd_buffer_pos] = '\0';
    }
    
    // Calculate the oldest available index
    int oldest_idx = (history_count > HISTORY_SIZE) ? (history_count - HISTORY_SIZE) : 0;
    
    // Move to older command
    if (history_index == -1) {
        history_index = history_count - 1;
    } else if (history_index > oldest_idx) {
        history_index--;
    } else {
        return; // Already at oldest
    }
    
    // Display the command from history
    int idx = history_index % HISTORY_SIZE;
    display_command(history[idx]);
}

// Navigate to next command in history (down arrow)
void tty_history_down(void) {
    if (history_index == -1) return; // Not navigating history
    
    if (history_index < history_count - 1) {
        // Move to newer command
        history_index++;
        int idx = history_index % HISTORY_SIZE;
        display_command(history[idx]);
    } else {
        // Return to current input
        history_index = -1;
        if (temp_cmd_saved) {
            display_command(temp_cmd_buffer);
            temp_cmd_saved = 0;
        } else {
            clear_command_line();
            cmd_buffer_pos = 0;
            cmd_cursor_pos = 0;
        }
    }
}

// Called when command is executed - add to history and reset navigation
void tty_history_commit(void) {
    history_add(cmd_buffer);
    history_index = -1;
    temp_cmd_saved = 0;
}

// Signal stop (Ctrl+D)
void tty_signal_stop(void) {
    stop_signal = 1;
    tty_putstr("^D\n");
    
    // Clear current command and show new prompt
    cmd_buffer_pos = 0;
    cmd_cursor_pos = 0;
    cmd_buffer[0] = '\0';
    
    // Show prompt again
    char path[64];
    fat32_get_current_path(path, 64);
    tty_putstr("DanOS:");
    tty_putstr(path);
    tty_putstr("$ ");
    
    // Update prompt position
    prompt_row = tty_row;
    prompt_column = tty_column;
}

// Check and clear stop signal
int tty_check_stop(void) {
    if (stop_signal) {
        stop_signal = 0;
        return 1;
    }
    return 0;
}

// Update prompt position (call this after displaying prompt)
void tty_set_prompt_position(void) {
    prompt_row = tty_row;
    prompt_column = tty_column;
}

// Print command history from .history file
void tty_print_history(void) {
    fat32_file_t file;
    
    if (fat32_open_file(".history", &file) != 0) {
        tty_putstr("No command history found.\n");
        tty_putstr("(History will be saved to .history file)\n");
        return;
    }
    
    if (file.file_size == 0) {
        tty_putstr("History file is empty.\n");
        return;
    }
    
    // Read the history file
    uint8_t buffer[4096];
    uint32_t bytes_to_read = file.file_size > 4095 ? 4095 : file.file_size;
    int bytes_read = fat32_read_file(&file, buffer, bytes_to_read);
    
    if (bytes_read <= 0) {
        tty_putstr("Error reading history file.\n");
        return;
    }
    
    buffer[bytes_read] = '\0';
    
    tty_putstr("Command History:\n");
    tty_putstr("----------------\n");
    
    // Hide cursor during history output
    if (fb_is_available()) {
        terminal_hide_cursor();
    }
    
    // Print line by line with line numbers
    int line_num = 1;
    int line_start = 0;
    
    for (int i = 0; i <= bytes_read - 1; i++) {
        if (buffer[i] == '\n' || buffer[i] == '\0') {
            // Print line number
            tty_putchar_internal(' ');
            tty_putchar_internal(' ');
            if (line_num < 10) {
                tty_putchar_internal(' ');
            }
            if (line_num < 100) {
                tty_putchar_internal(' ');
            }
            // Print number
            if (line_num >= 100) {
                tty_putchar_internal('0' + (line_num / 100) % 10);
            }
            if (line_num >= 10) {
                tty_putchar_internal('0' + (line_num / 10) % 10);
            }
            tty_putchar_internal('0' + line_num % 10);
            tty_putchar_internal(' ');
            tty_putchar_internal(' ');
            
            // Print the line content
            for (int j = line_start; j < i; j++) {
                if (buffer[j] >= 32 && buffer[j] < 127) {
                    tty_putchar_internal(buffer[j]);
                }
            }
            tty_putchar_internal('\n');
            
            line_start = i + 1;
            line_num++;
            
            if (buffer[i] == '\0') break;
        }
    }
    
    // Show cursor at end
    if (fb_is_available()) {
        terminal_set_cursor(tty_column, tty_row);
        terminal_draw_cursor();
    } else {
        set_cursor_offset(tty_row * screen_width + tty_column);
    }
}

// Helper: Redraw command line with selection highlighting
static void redraw_cmd_with_selection(void) {
    // Save current position
    tty_row = prompt_row;
    tty_column = prompt_column;
    
    int sel_min = selection_start < selection_end ? selection_start : selection_end;
    int sel_max = selection_start < selection_end ? selection_end : selection_start;
    
    // Redraw entire command with highlighting
    for (int i = 0; i < cmd_buffer_pos; i++) {
        uint8_t color;
        if (selection_active && i >= sel_min && i < sel_max) {
            // Highlighted (inverted colors)
            color = vga_entry_color(PRINT_COLOR_BLACK, PRINT_COLOR_YELLOW);
        } else {
            color = tty_color;
        }
        
        if (fb_is_available()) {
            terminal_draw_char_at(cmd_buffer[i], tty_column, tty_row,
                                  vga_to_rgb(color & 0x0F),
                                  vga_to_rgb((color >> 4) & 0x0F));
        } else {
            const size_t index = tty_row * screen_width + tty_column;
            tty_buffer[index] = vga_entry(cmd_buffer[i], color);
        }
        tty_column++;
        if (tty_column >= screen_width) {
            tty_column = 0;
            tty_row++;
        }
    }
    
    // Calculate cursor screen position
    tty_row = prompt_row;
    tty_column = prompt_column + cmd_cursor_pos;
    while (tty_column >= screen_width) {
        tty_column -= screen_width;
        tty_row++;
    }
    set_cursor_offset(tty_row * screen_width + tty_column);
}

// Start selection mode (Ctrl+Space)
void tty_start_selection(void) {
    selection_active = 1;
    
    if (editor_mode) {
        selection_start = editor_cursor_pos;
        selection_end = editor_cursor_pos;
        editor_full_redraw();
    } else {
        selection_start = cmd_cursor_pos;
        selection_end = cmd_cursor_pos;
        redraw_cmd_with_selection();
    }
}

// Update selection end when cursor moves
void tty_update_selection(void) {
    if (!selection_active) return;
    
    if (editor_mode) {
        selection_end = editor_cursor_pos;
        editor_full_redraw();
    } else {
        selection_end = cmd_cursor_pos;
        redraw_cmd_with_selection();
    }
}

// Cancel selection
void tty_cancel_selection(void) {
    if (!selection_active) return;
    
    selection_active = 0;
    selection_start = -1;
    selection_end = -1;
    
    if (editor_mode) {
        editor_full_redraw();
    } else {
        // Redraw without highlighting
        tty_row = prompt_row;
        tty_column = prompt_column;
        
        for (int i = 0; i < cmd_buffer_pos; i++) {
            if (fb_is_available()) {
                terminal_draw_char_at(cmd_buffer[i], tty_column, tty_row,
                                      vga_to_rgb(tty_color & 0x0F),
                                      vga_to_rgb((tty_color >> 4) & 0x0F));
            } else {
                const size_t index = tty_row * screen_width + tty_column;
                tty_buffer[index] = vga_entry(cmd_buffer[i], tty_color);
            }
            tty_column++;
            if (tty_column >= screen_width) {
                tty_column = 0;
                tty_row++;
            }
        }
        
        // Restore cursor position
        tty_row = prompt_row;
        tty_column = prompt_column + cmd_cursor_pos;
        while (tty_column >= screen_width) {
            tty_column -= screen_width;
            tty_row++;
        }
        set_cursor_offset(tty_row * screen_width + tty_column);
    }
}

// Copy selection to clipboard (Ctrl+C)
void tty_copy(void) {
    if (!selection_active || selection_start == selection_end) {
        // No selection - nothing to copy
        return;
    }
    
    int sel_min = selection_start < selection_end ? selection_start : selection_end;
    int sel_max = selection_start < selection_end ? selection_end : selection_start;
    
    // Copy to clipboard from appropriate buffer
    clipboard_len = 0;
    if (editor_mode) {
        for (int i = sel_min; i < sel_max && clipboard_len < CLIPBOARD_SIZE - 1; i++) {
            clipboard[clipboard_len++] = editor_buffer[i];
        }
    } else {
        for (int i = sel_min; i < sel_max && clipboard_len < CLIPBOARD_SIZE - 1; i++) {
            clipboard[clipboard_len++] = cmd_buffer[i];
        }
    }
    clipboard[clipboard_len] = '\0';
    
    // Cancel selection after copy
    tty_cancel_selection();
}

// Paste from clipboard (Ctrl+V)
void tty_paste(void) {
    if (clipboard_len == 0) return; // Nothing to paste
    
    // Cancel any active selection
    if (selection_active) {
        tty_cancel_selection();
    }
    
    if (editor_mode) {
        // Paste into editor buffer
        for (int c = 0; c < clipboard_len; c++) {
            if (editor_buffer_pos >= EDITOR_BUFFER_SIZE - 1) break;
            
            // Shift characters right
            for (int i = editor_buffer_pos; i > editor_cursor_pos; i--) {
                editor_buffer[i] = editor_buffer[i - 1];
            }
            editor_buffer[editor_cursor_pos] = clipboard[c];
            editor_buffer_pos++;
            editor_cursor_pos++;
        }
        
        // Redraw editor
        editor_full_redraw();
    } else {
        // Paste into command buffer
        for (int c = 0; c < clipboard_len; c++) {
            if (cmd_buffer_pos >= CMD_BUFFER_SIZE - 1) break;
            
            // Shift characters right
            for (int i = cmd_buffer_pos; i > cmd_cursor_pos; i--) {
                cmd_buffer[i] = cmd_buffer[i - 1];
            }
            cmd_buffer[cmd_cursor_pos] = clipboard[c];
            cmd_buffer_pos++;
            cmd_cursor_pos++;
        }
        
        // Redraw the command line
        tty_row = prompt_row;
        tty_column = prompt_column;
        
        for (int i = 0; i < cmd_buffer_pos; i++) {
            if (fb_is_available()) {
                terminal_draw_char_at(cmd_buffer[i], tty_column, tty_row,
                                      vga_to_rgb(tty_color & 0x0F),
                                      vga_to_rgb((tty_color >> 4) & 0x0F));
            } else {
                const size_t index = tty_row * screen_width + tty_column;
                tty_buffer[index] = vga_entry(cmd_buffer[i], tty_color);
            }
            tty_column++;
            if (tty_column >= screen_width) {
                tty_column = 0;
                tty_row++;
            }
        }
        
        // Clear any leftover characters
        if (fb_is_available()) {
            terminal_draw_char_at(' ', tty_column, tty_row,
                                  vga_to_rgb(tty_color & 0x0F),
                                  vga_to_rgb((tty_color >> 4) & 0x0F));
        } else {
            const size_t index = tty_row * screen_width + tty_column;
            tty_buffer[index] = vga_entry(' ', tty_color);
        }
        
        // Position cursor
        tty_row = prompt_row;
        tty_column = prompt_column + cmd_cursor_pos;
        while (tty_column >= screen_width) {
            tty_column -= screen_width;
            tty_row++;
        }
        set_cursor_offset(tty_row * screen_width + tty_column);
    }
}

// Check if selection is active
int tty_is_selecting(void) {
    return selection_active;
}

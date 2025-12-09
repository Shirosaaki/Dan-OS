//
// Framebuffer Terminal Header
// Replaces legacy VGA text mode with a linear framebuffer console
//

#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include <stdint.h>
#include <stddef.h>

// =============================================================================
// FRAMEBUFFER INFORMATION STRUCTURE
// =============================================================================
// This struct holds all information about the linear framebuffer.
// Values are populated from Multiboot2 framebuffer tag at boot time.

typedef struct {
    uint64_t address;       // Physical address of the framebuffer (linear)
    uint32_t width;         // Width in pixels
    uint32_t height;        // Height in pixels
    uint32_t pitch;         // Bytes per scanline (may include padding)
    uint8_t  bpp;           // Bits per pixel (typically 32 for BGRA)
    uint8_t  red_pos;       // Bit position of red channel
    uint8_t  red_mask;      // Bit size of red channel
    uint8_t  green_pos;     // Bit position of green channel
    uint8_t  green_mask;    // Bit size of green channel
    uint8_t  blue_pos;      // Bit position of blue channel
    uint8_t  blue_mask;     // Bit size of blue channel
} framebuffer_info_t;

// =============================================================================
// FONT CONFIGURATION
// =============================================================================
// We use a simple 8x16 bitmap font (8 pixels wide, 16 pixels tall per character)

#define FONT_WIDTH  8
#define FONT_HEIGHT 16

// =============================================================================
// TERMINAL STATE STRUCTURE
// =============================================================================
// Manages the text console state on top of the framebuffer

typedef struct {
    size_t cursor_x;        // Current column (in characters)
    size_t cursor_y;        // Current row (in characters)
    size_t cols;            // Total columns (width / FONT_WIDTH)
    size_t rows;            // Total rows (height / FONT_HEIGHT)
    uint32_t fg_color;      // Foreground color (32-bit ARGB/RGB)
    uint32_t bg_color;      // Background color (32-bit ARGB/RGB)
} terminal_state_t;

// =============================================================================
// COLOR DEFINITIONS (32-bit RGB format: 0x00RRGGBB)
// =============================================================================
// These match the VGA color palette for easy migration

#define FB_COLOR_BLACK        0x00000000
#define FB_COLOR_BLUE         0x000000AA
#define FB_COLOR_GREEN        0x0000AA00
#define FB_COLOR_CYAN         0x0000AAAA
#define FB_COLOR_RED          0x00AA0000
#define FB_COLOR_MAGENTA      0x00AA00AA
#define FB_COLOR_BROWN        0x00AA5500
#define FB_COLOR_LIGHT_GREY   0x00AAAAAA
#define FB_COLOR_DARK_GREY    0x00555555
#define FB_COLOR_LIGHT_BLUE   0x005555FF
#define FB_COLOR_LIGHT_GREEN  0x0055FF55
#define FB_COLOR_LIGHT_CYAN   0x0055FFFF
#define FB_COLOR_LIGHT_RED    0x00FF5555
#define FB_COLOR_PINK         0x00FF55FF
#define FB_COLOR_YELLOW       0x00FFFF55
#define FB_COLOR_WHITE        0x00FFFFFF

// =============================================================================
// FRAMEBUFFER INITIALIZATION
// =============================================================================

/**
 * Initialize the framebuffer subsystem.
 * Call this early in kernel_main with parameters from Multiboot2.
 * 
 * @param addr   Physical address of framebuffer
 * @param width  Width in pixels
 * @param height Height in pixels
 * @param pitch  Bytes per scanline
 * @param bpp    Bits per pixel (16, 24, or 32)
 */
void fb_init(uint64_t addr, uint32_t width, uint32_t height, 
             uint32_t pitch, uint8_t bpp);

/**
 * Check if framebuffer is available and initialized.
 * @return 1 if framebuffer is ready, 0 otherwise
 */
int fb_is_available(void);

/**
 * Get framebuffer info structure (read-only access).
 */
const framebuffer_info_t* fb_get_info(void);

// =============================================================================
// LOW-LEVEL PIXEL OPERATIONS
// =============================================================================

/**
 * Put a single pixel at (x, y) with the specified color.
 * Color format depends on bpp but we use 32-bit ARGB internally.
 */
void fb_putpixel(uint32_t x, uint32_t y, uint32_t color);

/**
 * Fill a rectangle with a solid color.
 * Used for clearing character cells and scrolling.
 */
void fb_fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, 
                  uint32_t color);

/**
 * Copy a rectangular region (used for scrolling).
 * Copies from (src_x, src_y) to (dst_x, dst_y).
 */
void fb_copy_rect(uint32_t dst_x, uint32_t dst_y,
                  uint32_t src_x, uint32_t src_y,
                  uint32_t width, uint32_t height);

// =============================================================================
// TERMINAL API (TEXT CONSOLE ON FRAMEBUFFER)
// =============================================================================

/**
 * Initialize the terminal subsystem.
 * Must be called after fb_init().
 * Sets up cursor position, colors, and clears the screen.
 */
void terminal_init(void);

/**
 * Clear the entire terminal screen.
 * Fills with background color and resets cursor to (0, 0).
 */
void terminal_clear(void);

/**
 * Set the foreground (text) color.
 */
void terminal_set_fg(uint32_t color);

/**
 * Set the background color.
 */
void terminal_set_bg(uint32_t color);

/**
 * Set both foreground and background colors.
 */
void terminal_set_colors(uint32_t fg, uint32_t bg);

/**
 * Put a single character at the current cursor position.
 * Handles special characters: '\n' (newline), '\r' (carriage return),
 * '\t' (tab), '\b' (backspace).
 * Advances cursor and scrolls if needed.
 */
void terminal_putc(char c);

/**
 * Write a null-terminated string to the terminal.
 */
void terminal_write(const char* str);

/**
 * Write a string with specified length (may contain null bytes).
 */
void terminal_write_len(const char* str, size_t len);

/**
 * Scroll the terminal up by one line.
 * Copies all lines up and clears the bottom line.
 */
void terminal_scroll(void);

/**
 * Draw a character at a specific character cell position.
 * Does not move the cursor.
 */
void terminal_draw_char_at(char c, size_t col, size_t row, 
                           uint32_t fg, uint32_t bg);

/**
 * Set cursor position (in character coordinates).
 */
void terminal_set_cursor(size_t col, size_t row);

/**
 * Get current cursor column.
 */
size_t terminal_get_col(void);

/**
 * Get current cursor row.
 */
size_t terminal_get_row(void);

/**
 * Get total number of columns.
 */
size_t terminal_get_cols(void);

/**
 * Get total number of rows.
 */
size_t terminal_get_rows(void);

/**
 * Draw a visible cursor (blinking block or underline).
 * Call this periodically for cursor visibility.
 */
void terminal_draw_cursor(void);

/**
 * Hide the cursor (erase cursor graphic).
 */
void terminal_hide_cursor(void);

// =============================================================================
// MULTIBOOT2 FRAMEBUFFER PARSING
// =============================================================================

/**
 * Parse Multiboot2 info structure and initialize framebuffer.
 * Returns 0 on success, -1 if no framebuffer tag found.
 */
int fb_init_from_multiboot2(void* multiboot_info);

#endif // FRAMEBUFFER_H

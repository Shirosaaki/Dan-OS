//
// Framebuffer Terminal Implementation
// Software text rendering on a linear framebuffer
//

#include <kernel/drivers/framebuffer.h>
#include <kernel/drivers/font8x16.h>
#include <stddef.h>

// =============================================================================
// GLOBAL STATE
// =============================================================================

// Framebuffer information (populated from bootloader)
static framebuffer_info_t fb_info;
static int fb_available = 0;

// Terminal state
static terminal_state_t term_state;

// Pointer to framebuffer memory (cast from physical address)
// Note: In a real OS with paging, you'd map this address appropriately
static volatile uint8_t* fb_ptr = NULL;

// =============================================================================
// FRAMEBUFFER INITIALIZATION
// =============================================================================

void fb_init(uint64_t addr, uint32_t width, uint32_t height, 
             uint32_t pitch, uint8_t bpp) {
    fb_info.address = addr;
    fb_info.width = width;
    fb_info.height = height;
    fb_info.pitch = pitch;
    fb_info.bpp = bpp;
    
    // Default color positions for 32-bit BGRA (common format)
    // These may need adjustment based on actual framebuffer format
    fb_info.blue_pos = 0;
    fb_info.blue_mask = 8;
    fb_info.green_pos = 8;
    fb_info.green_mask = 8;
    fb_info.red_pos = 16;
    fb_info.red_mask = 8;
    
    // Set framebuffer pointer
    // In identity-mapped or direct physical access scenarios:
    fb_ptr = (volatile uint8_t*)(uintptr_t)addr;
    
    fb_available = 1;
}

int fb_is_available(void) {
    return fb_available;
}

const framebuffer_info_t* fb_get_info(void) {
    return &fb_info;
}

// =============================================================================
// LOW-LEVEL PIXEL OPERATIONS
// =============================================================================

// Internal: Calculate byte offset for a pixel at (x, y)
static inline uint64_t fb_pixel_offset(uint32_t x, uint32_t y) {
    return (uint64_t)y * fb_info.pitch + (uint64_t)x * (fb_info.bpp / 8);
}

void fb_putpixel(uint32_t x, uint32_t y, uint32_t color) {
    if (!fb_available || x >= fb_info.width || y >= fb_info.height) {
        return;
    }
    
    uint64_t offset = fb_pixel_offset(x, y);
    
    switch (fb_info.bpp) {
        case 32: {
            // 32-bit color (BGRA or RGBA)
            uint32_t* pixel = (uint32_t*)(fb_ptr + offset);
            *pixel = color;
            break;
        }
        case 24: {
            // 24-bit color (BGR)
            fb_ptr[offset + 0] = (color >> 0) & 0xFF;   // Blue
            fb_ptr[offset + 1] = (color >> 8) & 0xFF;   // Green
            fb_ptr[offset + 2] = (color >> 16) & 0xFF;  // Red
            break;
        }
        case 16: {
            // 16-bit color (RGB565)
            uint16_t* pixel = (uint16_t*)(fb_ptr + offset);
            uint8_t r = ((color >> 16) & 0xFF) >> 3;  // 5 bits
            uint8_t g = ((color >> 8) & 0xFF) >> 2;   // 6 bits
            uint8_t b = (color & 0xFF) >> 3;          // 5 bits
            *pixel = (r << 11) | (g << 5) | b;
            break;
        }
        default:
            break;
    }
}

void fb_fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, 
                  uint32_t color) {
    if (!fb_available) return;
    
    // Clamp to screen bounds
    if (x >= fb_info.width || y >= fb_info.height) return;
    if (x + width > fb_info.width) width = fb_info.width - x;
    if (y + height > fb_info.height) height = fb_info.height - y;
    
    // Optimized fill for 32-bit framebuffer
    if (fb_info.bpp == 32) {
        for (uint32_t row = 0; row < height; row++) {
            uint32_t* line = (uint32_t*)(fb_ptr + fb_pixel_offset(x, y + row));
            for (uint32_t col = 0; col < width; col++) {
                line[col] = color;
            }
        }
    } else {
        // Fallback for other bpp
        for (uint32_t row = 0; row < height; row++) {
            for (uint32_t col = 0; col < width; col++) {
                fb_putpixel(x + col, y + row, color);
            }
        }
    }
}

void fb_copy_rect(uint32_t dst_x, uint32_t dst_y,
                  uint32_t src_x, uint32_t src_y,
                  uint32_t width, uint32_t height) {
    if (!fb_available) return;
    
    // Handle overlapping regions by copying in correct direction
    // For scrolling up, dst_y < src_y, so we copy top-to-bottom
    
    uint32_t bytes_per_pixel = fb_info.bpp / 8;
    uint32_t row_bytes = width * bytes_per_pixel;
    
    if (dst_y <= src_y) {
        // Copy top to bottom
        for (uint32_t row = 0; row < height; row++) {
            uint8_t* dst = (uint8_t*)(fb_ptr + fb_pixel_offset(dst_x, dst_y + row));
            uint8_t* src = (uint8_t*)(fb_ptr + fb_pixel_offset(src_x, src_y + row));
            
            // Simple byte copy (memmove equivalent)
            for (uint32_t i = 0; i < row_bytes; i++) {
                dst[i] = src[i];
            }
        }
    } else {
        // Copy bottom to top (for scrolling down)
        for (int32_t row = height - 1; row >= 0; row--) {
            uint8_t* dst = (uint8_t*)(fb_ptr + fb_pixel_offset(dst_x, dst_y + row));
            uint8_t* src = (uint8_t*)(fb_ptr + fb_pixel_offset(src_x, src_y + row));
            
            for (uint32_t i = 0; i < row_bytes; i++) {
                dst[i] = src[i];
            }
        }
    }
}

// =============================================================================
// CHARACTER DRAWING
// =============================================================================

// Draw a single character at pixel position (px, py) using the bitmap font
static void fb_draw_char(uint32_t px, uint32_t py, char c, 
                         uint32_t fg_color, uint32_t bg_color) {
    // Handle non-printable characters
    if (c < 32 || c > 127) {
        c = '?';  // Replace with placeholder
    }
    
    // Get font bitmap for this character
    const uint8_t* glyph = &font8x16_data[(c - FONT8X16_FIRST_CHAR) * FONT8X16_HEIGHT];
    
    // Draw each row of the character
    for (int row = 0; row < FONT8X16_HEIGHT; row++) {
        uint8_t bits = glyph[row];
        
        for (int col = 0; col < FONT8X16_WIDTH; col++) {
            // Check if this pixel should be foreground or background
            // Bit 7 is leftmost pixel
            uint32_t color = (bits & (0x80 >> col)) ? fg_color : bg_color;
            fb_putpixel(px + col, py + row, color);
        }
    }
}

// =============================================================================
// TERMINAL INITIALIZATION
// =============================================================================

void terminal_init(void) {
    if (!fb_available) return;
    
    // Calculate terminal dimensions based on framebuffer size and font
    term_state.cols = fb_info.width / FONT_WIDTH;
    term_state.rows = fb_info.height / FONT_HEIGHT;
    term_state.cursor_x = 0;
    term_state.cursor_y = 0;
    
    // Default colors: yellow on black (matching your original VGA theme)
    term_state.fg_color = FB_COLOR_YELLOW;
    term_state.bg_color = FB_COLOR_BLACK;
    
    // Clear the screen
    terminal_clear();
}

// =============================================================================
// TERMINAL OPERATIONS
// =============================================================================

void terminal_clear(void) {
    if (!fb_available) return;
    
    // Fill entire screen with background color
    fb_fill_rect(0, 0, fb_info.width, fb_info.height, term_state.bg_color);
    
    // Reset cursor
    term_state.cursor_x = 0;
    term_state.cursor_y = 0;
}

void terminal_set_fg(uint32_t color) {
    term_state.fg_color = color;
}

void terminal_set_bg(uint32_t color) {
    term_state.bg_color = color;
}

void terminal_set_colors(uint32_t fg, uint32_t bg) {
    term_state.fg_color = fg;
    term_state.bg_color = bg;
}

void terminal_scroll(void) {
    if (!fb_available) return;
    
    // Calculate pixel coordinates
    uint32_t line_height = FONT_HEIGHT;
    uint32_t scroll_height = (term_state.rows - 1) * line_height;
    
    // Copy all lines up by one line
    // Source: row 1, Destination: row 0
    fb_copy_rect(0, 0,                    // destination (0, 0)
                 0, line_height,           // source (0, FONT_HEIGHT)
                 fb_info.width, scroll_height);
    
    // Clear the last line
    fb_fill_rect(0, scroll_height,        // position of last line
                 fb_info.width, line_height,
                 term_state.bg_color);
}

void terminal_draw_char_at(char c, size_t col, size_t row, 
                           uint32_t fg, uint32_t bg) {
    if (!fb_available) return;
    if (col >= term_state.cols || row >= term_state.rows) return;
    
    // Calculate pixel position
    uint32_t px = col * FONT_WIDTH;
    uint32_t py = row * FONT_HEIGHT;
    
    fb_draw_char(px, py, c, fg, bg);
}

void terminal_putc(char c) {
    if (!fb_available) return;
    
    // Handle special characters
    switch (c) {
        case '\n':
            // Newline: move to start of next line
            term_state.cursor_x = 0;
            term_state.cursor_y++;
            break;
            
        case '\r':
            // Carriage return: move to start of current line
            term_state.cursor_x = 0;
            break;
            
        case '\t':
            // Tab: advance to next 8-column boundary
            term_state.cursor_x = (term_state.cursor_x + 8) & ~7;
            if (term_state.cursor_x >= term_state.cols) {
                term_state.cursor_x = 0;
                term_state.cursor_y++;
            }
            break;
            
        case '\b':
            // Backspace: move cursor back one position
            if (term_state.cursor_x > 0) {
                term_state.cursor_x--;
            } else if (term_state.cursor_y > 0) {
                term_state.cursor_y--;
                term_state.cursor_x = term_state.cols - 1;
            }
            // Erase character at new position
            terminal_draw_char_at(' ', term_state.cursor_x, term_state.cursor_y,
                                  term_state.fg_color, term_state.bg_color);
            break;
            
        default:
            // Regular printable character
            if (c >= 32 && c < 127) {
                terminal_draw_char_at(c, term_state.cursor_x, term_state.cursor_y,
                                      term_state.fg_color, term_state.bg_color);
                term_state.cursor_x++;
                
                // Handle line wrap
                if (term_state.cursor_x >= term_state.cols) {
                    term_state.cursor_x = 0;
                    term_state.cursor_y++;
                }
            }
            break;
    }
    
    // Handle scrolling if cursor went past bottom of screen
    while (term_state.cursor_y >= term_state.rows) {
        terminal_scroll();
        term_state.cursor_y--;
    }
}

void terminal_write(const char* str) {
    if (!str) return;
    
    while (*str) {
        terminal_putc(*str++);
    }
}

void terminal_write_len(const char* str, size_t len) {
    if (!str) return;
    
    for (size_t i = 0; i < len; i++) {
        terminal_putc(str[i]);
    }
}

void terminal_set_cursor(size_t col, size_t row) {
    if (col < term_state.cols) {
        term_state.cursor_x = col;
    }
    if (row < term_state.rows) {
        term_state.cursor_y = row;
    }
}

size_t terminal_get_col(void) {
    return term_state.cursor_x;
}

size_t terminal_get_row(void) {
    return term_state.cursor_y;
}

size_t terminal_get_cols(void) {
    return term_state.cols;
}

size_t terminal_get_rows(void) {
    return term_state.rows;
}

// Cursor state for blinking
static int cursor_visible = 0;
static size_t cursor_last_x = 0;
static size_t cursor_last_y = 0;

void terminal_draw_cursor(void) {
    if (!fb_available) return;
    
    // Save cursor position for hiding later
    cursor_last_x = term_state.cursor_x;
    cursor_last_y = term_state.cursor_y;
    
    // Draw an underline cursor at the bottom of the character cell
    uint32_t px = term_state.cursor_x * FONT_WIDTH;
    uint32_t py = term_state.cursor_y * FONT_HEIGHT + FONT_HEIGHT - 2;
    
    // Draw cursor as a 2-pixel tall underline
    fb_fill_rect(px, py, FONT_WIDTH, 2, term_state.fg_color);
    cursor_visible = 1;
}

void terminal_hide_cursor(void) {
    if (!fb_available || !cursor_visible) return;
    
    // Erase cursor by redrawing background at old cursor underline position
    uint32_t px = cursor_last_x * FONT_WIDTH;
    uint32_t py = cursor_last_y * FONT_HEIGHT + FONT_HEIGHT - 2;
    
    // Clear the cursor underline with background color
    fb_fill_rect(px, py, FONT_WIDTH, 2, term_state.bg_color);
    cursor_visible = 0;
}

// =============================================================================
// MULTIBOOT2 FRAMEBUFFER PARSING
// =============================================================================

// Multiboot2 tag types
#define MULTIBOOT2_TAG_TYPE_END            0
#define MULTIBOOT2_TAG_TYPE_FRAMEBUFFER    8

// Multiboot2 tag header
struct multiboot2_tag {
    uint32_t type;
    uint32_t size;
};

// Multiboot2 framebuffer tag
struct multiboot2_tag_framebuffer {
    uint32_t type;
    uint32_t size;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t  framebuffer_bpp;
    uint8_t  framebuffer_type;
    uint8_t  reserved;
    // Color info follows for RGB type
    uint8_t  framebuffer_red_field_position;
    uint8_t  framebuffer_red_mask_size;
    uint8_t  framebuffer_green_field_position;
    uint8_t  framebuffer_green_mask_size;
    uint8_t  framebuffer_blue_field_position;
    uint8_t  framebuffer_blue_mask_size;
};

int fb_init_from_multiboot2(void* multiboot_info) {
    if (!multiboot_info) return -1;
    
    // Multiboot2 info structure starts with total size
    uint32_t total_size = *(uint32_t*)multiboot_info;
    
    // Sanity check: size should be reasonable
    if (total_size < 8 || total_size > 0x100000) return -1;
    
    // Tags start at offset 8 (after size and reserved fields)
    struct multiboot2_tag* tag = (struct multiboot2_tag*)((uint8_t*)multiboot_info + 8);
    uint8_t* end_ptr = (uint8_t*)multiboot_info + total_size;
    
    while ((uint8_t*)tag < end_ptr && tag->type != MULTIBOOT2_TAG_TYPE_END) {
        // Sanity check tag size
        if (tag->size < 8 || tag->size > total_size) break;
        
        if (tag->type == MULTIBOOT2_TAG_TYPE_FRAMEBUFFER) {
            struct multiboot2_tag_framebuffer* fb_tag = 
                (struct multiboot2_tag_framebuffer*)tag;
            
            // Validate framebuffer values
            if (fb_tag->framebuffer_width == 0 || 
                fb_tag->framebuffer_height == 0 ||
                fb_tag->framebuffer_bpp == 0 ||
                fb_tag->framebuffer_addr == 0) {
                break;  // Invalid framebuffer data
            }
            
            // Initialize framebuffer with parsed values
            fb_info.address = fb_tag->framebuffer_addr;
            fb_info.width = fb_tag->framebuffer_width;
            fb_info.height = fb_tag->framebuffer_height;
            fb_info.pitch = fb_tag->framebuffer_pitch;
            fb_info.bpp = fb_tag->framebuffer_bpp;
            
            // Store color field information
            fb_info.red_pos = fb_tag->framebuffer_red_field_position;
            fb_info.red_mask = fb_tag->framebuffer_red_mask_size;
            fb_info.green_pos = fb_tag->framebuffer_green_field_position;
            fb_info.green_mask = fb_tag->framebuffer_green_mask_size;
            fb_info.blue_pos = fb_tag->framebuffer_blue_field_position;
            fb_info.blue_mask = fb_tag->framebuffer_blue_mask_size;
            
            // Set framebuffer pointer
            fb_ptr = (volatile uint8_t*)(uintptr_t)fb_info.address;
            fb_available = 1;
            
            return 0;  // Success
        }
        
        // Move to next tag (aligned to 8 bytes)
        uint32_t tag_size = (tag->size + 7) & ~7;
        tag = (struct multiboot2_tag*)((uint8_t*)tag + tag_size);
    }
    
    return -1;  // Framebuffer tag not found
}

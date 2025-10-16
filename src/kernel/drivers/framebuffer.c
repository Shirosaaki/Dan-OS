// Basic framebuffer driver using Multiboot2 framebuffer tag (GRUB)
#include "framebuffer.h"
#include "font8x8.h"
#include "../tty/../includes/string.h"
#include "../tty/../includes/tty.h"
#include <stdint.h>
#include <stddef.h>

// Minimal multiboot2 tag parsing (only framebuffer tag)
struct mb2_tag {
    uint32_t type;
    uint32_t size;
};

static framebuffer_info_t fb;
static int fb_initialized = 0;
static int fb_visible = 1;

// helpers to read unaligned values
static inline uint32_t read_u32(uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// Minimal hex printer using tty_putstr (prints 0x then 16 hex digits)
static void tty_puthex64_local(uint64_t v) {
    char buf[19];
    const char *hex = "0123456789ABCDEF";
    buf[0] = '0'; buf[1] = 'x';
    for (int i = 0; i < 16; ++i) {
        buf[17 - i] = hex[v & 0xF];
        v >>= 4;
    }
    buf[18] = '\0';
    // try to call tty_putstr if available
    extern void tty_putstr(const char*);
    tty_putstr(buf);
}

int fb_init(void *multiboot_info_ptr) {
    if (!multiboot_info_ptr) return -1;
    uint8_t *mb = (uint8_t *)multiboot_info_ptr;
    uint32_t total_size = read_u32(mb); // first 4 bytes = total size
    if (total_size == 0) return -1;

    uint32_t offset = 8; // skip total_size and reserved
    while (offset + 8 <= total_size) {
        struct mb2_tag *tag = (struct mb2_tag *)(mb + offset);
        if (tag->type == 0) break; // end tag

        // Debug: print tag type and size
        // tty_putstr is available but be careful early; we'll just print minimal info
        // (Use tty_putstr only if available)
        // tty_putstr("Found tag type: ");

        if (tag->type == 8) { // framebuffer tag
            uint8_t *tdata = (uint8_t *)tag;
            uint64_t addr = 0;
            for (int i = 0; i < 8; ++i) addr |= ((uint64_t)tdata[8 + i]) << (i*8);
            uint32_t pitch = read_u32(tdata + 16);
            uint32_t width = read_u32(tdata + 20);
            uint32_t height = read_u32(tdata + 24);
            uint8_t bpp = tdata[28];
            uint8_t type = tdata[29];

            fb.addr = (void *)(uintptr_t)addr;
            fb.pitch = pitch;
            fb.width = width;
            fb.height = height;
            fb.bpp = bpp;
            fb.type = type;

            // Debug print
            extern void tty_putstr(const char*);
            tty_putstr("framebuffer tag: addr=");
            tty_puthex64_local((uint64_t)addr);
            tty_putstr(" width=");
            // print width, height, bpp, pitch roughly
            char numbuf[32];
            int numlen = 0;
            uint32_t tmp = width;
            // convert width to decimal (simple)
            if (tmp == 0) { numbuf[numlen++] = '0'; }
            else {
                char rev[16]; int r = 0;
                while (tmp) { rev[r++] = '0' + (tmp % 10); tmp /= 10; }
                while (r--) numbuf[numlen++] = rev[r];
            }
            numbuf[numlen] = '\0';
            tty_putstr(numbuf);
            tty_putstr(" height=");
            // height
            numlen = 0; tmp = height; if (tmp == 0) { numbuf[numlen++] = '0'; } else { char rev[16]; int r = 0; while (tmp) { rev[r++] = '0' + (tmp % 10); tmp /= 10; } while (r--) numbuf[numlen++] = rev[r]; } numbuf[numlen] = '\0'; tty_putstr(numbuf);
            tty_putstr(" bpp="); numlen = 0; tmp = bpp; if (tmp == 0) { numbuf[numlen++] = '0'; } else { char rev[16]; int r = 0; while (tmp) { rev[r++] = '0' + (tmp % 10); tmp /= 10; } while (r--) numbuf[numlen++] = rev[r]; } numbuf[numlen] = '\0'; tty_putstr(numbuf);
            tty_putstr(" pitch="); numlen = 0; tmp = pitch; if (tmp == 0) { numbuf[numlen++] = '0'; } else { char rev[16]; int r = 0; while (tmp) { rev[r++] = '0' + (tmp % 10); tmp /= 10; } while (r--) numbuf[numlen++] = rev[r]; } numbuf[numlen] = '\0'; tty_putstr(numbuf);
            tty_putstr("\n");

            // Accept common bpp values: 32, 24, 16, and 8 (indexed)
            if (fb.bpp != 32 && fb.bpp != 24 && fb.bpp != 16 && fb.bpp != 8) {
                // unsupported bpp, continue searching
            } else {
                fb_initialized = 1;
                return 0;
            }
        }

        uint32_t sz = tag->size;
        if (sz == 0) break;
        offset += (sz + 7) & ~7u;
    }

    return -1;
}

void fb_set_visible(int v) {
    fb_visible = v ? 1 : 0;
    if (!fb_initialized) return;
    if (!fb_visible) {
        // If hiding framebuffer, clear it to avoid ghosting
        fb_fill_rect(0, 0, fb.width, fb.height, 0x00000000);
    } else {
        // restore welcome screen when shown
        fb_draw_welcome();
    }
}

int fb_is_visible(void) {
    return fb_visible;
}

void fb_draw_welcome(void) {
    if (!fb_initialized) return;
    fb_fill_rect(0, 0, fb.width, fb.height, 0x00000000);
    const char *msg = "Hello from DAN !";
    uint32_t tiny_char_w = 4;
    uint32_t x = (fb.width / 2) - (tiny_char_w * (uint32_t)(strlength(msg) / 2));
    uint32_t y = fb.height / 2 - 2;
    fb_puts_tiny(x, y, msg, 0x00FFA500);
}

void fb_get_info(framebuffer_info_t *out) {
    if (!out) return;
    *out = fb;
}

void fb_putpixel(uint32_t x, uint32_t y, uint32_t color) {
    if (!fb_initialized) return;
    if (x >= fb.width || y >= fb.height) return;
    uint8_t *base = (uint8_t *)fb.addr;
    uint8_t *pixel = base + y * fb.pitch + x * (fb.bpp / 8);
    // color input is 0xRRGGBB
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;
    if (fb.bpp == 32) {
        pixel[0] = b;
        pixel[1] = g;
        pixel[2] = r;
        pixel[3] = 0;
    } else if (fb.bpp == 24) {
        pixel[0] = b;
        pixel[1] = g;
        pixel[2] = r;
    } else if (fb.bpp == 8) {
        // Simple mapping: pack reduced RGB into an 8-bit index.
        // This is a heuristic to get distinct indices for R/G/B.
        uint8_t idx = ((r >> 5) << 5) | ((g >> 5) << 2) | (b >> 6);
        pixel[0] = idx;
    } else if (fb.bpp == 16) {
        // convert to RGB565
        uint16_t rgb565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
        pixel[0] = rgb565 & 0xFF;
        pixel[1] = (rgb565 >> 8) & 0xFF;
    }
}



void fb_putc(uint32_t x, uint32_t y, char c, uint32_t color) {
    if (!fb_initialized) return;
    if ((unsigned char)c >= 128) return;
    const uint8_t *glyph = font8x8_basic[(unsigned char)c];
    for (int row = 0; row < 8; ++row) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; ++col) {
            if (bits & (1 << col)) {
                fb_putpixel(x + col, y + row, color);
            }
        }
    }
}

// Draw a half-size character by sampling the 8x8 glyph into a 4x4 block.
void fb_putc_small(uint32_t x, uint32_t y, char c, uint32_t color) {
    if (!fb_initialized) return;
    if ((unsigned char)c >= 128) return;
    const uint8_t *glyph = font8x8_basic[(unsigned char)c];
    // We map 8x8 -> 4x4 by grouping each 2x2 pixels -> one pixel if any bit set
    for (int gy = 0; gy < 4; ++gy) {
        for (int gx = 0; gx < 4; ++gx) {
            // check corresponding 2x2 block in glyph
            int src_x = gx * 2;
            int src_y = gy * 2;
            int set = 0;
            for (int yy = 0; yy < 2; ++yy) {
                uint8_t bits = glyph[src_y + yy];
                for (int xx = 0; xx < 2; ++xx) {
                    if (bits & (1 << (src_x + xx))) { set = 1; break; }
                }
                if (set) break;
            }
            if (set) {
                fb_putpixel(x + gx, y + gy, color);
            }
        }
    }
}

// Helper: draw a small string where each char is 4 pixels wide and 4 high
void fb_puts_small(uint32_t x, uint32_t y, const char *s, uint32_t color) {
    while (*s) {
        fb_putc_small(x, y, *s++, color);
        x += 5; // 4 pixels + 1 spacing
    }
}

// Tiny renderer: map 8x8 glyph to ~3x4 by sampling columns and rows
void fb_putc_tiny(uint32_t x, uint32_t y, char c, uint32_t color) {
    if (!fb_initialized) return;
    if ((unsigned char)c >= 128) return;
    const uint8_t *glyph = font8x8_basic[(unsigned char)c];
    // sample 3 columns from the 8 columns (at positions 1, 3, 5)
    int cols[3] = {1, 3, 5};
    // sample 4 rows from the 8 rows (at positions 1, 3, 5, 7)
    int rows[4] = {1, 3, 5, 7};
    for (int ry = 0; ry < 4; ++ry) {
        uint8_t bits = glyph[rows[ry]];
        for (int cx = 0; cx < 3; ++cx) {
            int bit = cols[cx];
            if (bits & (1 << bit)) fb_putpixel(x + cx, y + ry, color);
        }
    }
}

void fb_puts_tiny(uint32_t x, uint32_t y, const char *s, uint32_t color) {
    while (*s) {
        fb_putc_tiny(x, y, *s++, color);
        x += 4; // 3 pixels + 1 spacing
    }
}

// Map a 24-bit RGB color to a simple 8-bit index. This is a heuristic
// mapping to produce distinct visible colors when the framebuffer is
// paletted/8bpp. If a true palette is available from the bootloader we'd
// use it; here we produce an index by reducing each channel.
static inline uint8_t rgb_to_8bit_index(uint8_t r, uint8_t g, uint8_t b) {
    // 3 bits red, 3 bits green, 2 bits blue -> RRR GGG BB
    return (uint8_t)(((r >> 5) << 5) | ((g >> 5) << 2) | (b >> 6));
}

void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    if (!fb_initialized) return;
    if (x >= fb.width || y >= fb.height) return;
    uint32_t maxw = fb.width - x;
    uint32_t maxh = fb.height - y;
    if (w > maxw) w = maxw;
    if (h > maxh) h = maxh;

    // Fast path for 8bpp where we can write full rows
    if (fb.bpp == 8) {
        uint8_t idx = rgb_to_8bit_index((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
        for (uint32_t yy = y; yy < y + h; ++yy) {
            uint8_t *row = (uint8_t *)fb.addr + yy * fb.pitch + x;
            for (uint32_t xx = 0; xx < w; ++xx) row[xx] = idx;
        }
        return;
    }

    // Generic (slower) path
    for (uint32_t yy = y; yy < y + h; ++yy) {
        for (uint32_t xx = x; xx < x + w; ++xx) {
            fb_putpixel(xx, yy, color);
        }
    }
}

// Draw a set of diagnostic panels covering the left side of the screen
// Each panel uses a different assumed pixel format/order so visually
// we can determine which interpretation matches the display.
void fb_draw_diagnostics(void) {
    if (!fb_initialized) return;
    uint32_t panel_w = fb.width / 6;
    uint32_t h = fb.height;

    // Panel 0: 32bpp assumed (B,G,R)
    for (uint32_t x = 0; x < panel_w; ++x) {
        uint32_t color = 0x00FF0000; // red
        fb_fill_rect(x, 0, 1, h, color);
    }

    // Panel 1: 24bpp assumed order
    for (uint32_t x = panel_w; x < 2*panel_w; ++x) {
        uint32_t color = 0x0000FF00; // green
        fb_fill_rect(x, 0, 1, h, color);
    }

    // Panel 2: 16bpp (RGB565) pattern
    for (uint32_t x = 2*panel_w; x < 3*panel_w; ++x) {
        uint32_t color = 0x000000FF; // blue
        fb_fill_rect(x, 0, 1, h, color);
    }

    // Panel 3: 8bpp heuristic mapping
    for (uint32_t x = 3*panel_w; x < 4*panel_w; ++x) {
        uint32_t color = 0x00FFFF00; // yellow
        fb_fill_rect(x, 0, 1, h, color);
    }

    // Panel 4: attempt swapped byte order (R and B swapped)
    for (uint32_t x = 4*panel_w; x < 5*panel_w; ++x) {
        uint32_t color = 0x0000FFFF; // cyan
        fb_fill_rect(x, 0, 1, h, color);
    }

    // Panel 5: white
    for (uint32_t x = 5*panel_w; x < fb.width; ++x) {
        uint32_t color = 0x00FFFFFF;
        fb_fill_rect(x, 0, 1, h, color);
    }
}

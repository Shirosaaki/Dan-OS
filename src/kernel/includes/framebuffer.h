// Simple framebuffer API header
#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include <stdint.h>

typedef struct framebuffer_info {
    void *addr;      // linear framebuffer address
    uint32_t width;
    uint32_t height;
    uint32_t pitch;  // bytes per scanline
    uint32_t bpp;    // bits per pixel
    uint32_t type;   // color type
} framebuffer_info_t;

int fb_init(void *multiboot_info_ptr);
void fb_get_info(framebuffer_info_t *out);
void fb_putpixel(uint32_t x, uint32_t y, uint32_t color);
void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void fb_putc(uint32_t x, uint32_t y, char c, uint32_t color);
// Draw half-size character (approx 4x4) using the 8x8 font
void fb_putc_small(uint32_t x, uint32_t y, char c, uint32_t color);
void fb_draw_diagnostics(void);
// Even smaller (tiny) renderer: ~3x4 per character
void fb_putc_tiny(uint32_t x, uint32_t y, char c, uint32_t color);
void fb_puts_tiny(uint32_t x, uint32_t y, const char *s, uint32_t color);
// Control whether framebuffer output is currently visible (1) or hidden (0)
void fb_set_visible(int v);
int fb_is_visible(void);

// Draw the standard welcome screen (black bg + centered orange text)
void fb_draw_welcome(void);

#endif // FRAMEBUFFER_H

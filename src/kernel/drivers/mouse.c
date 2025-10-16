#include "mouse.h"
#include "framebuffer.h"
#include "keyboard.h"
#include "../../cpu/ports.h"
#include <stdint.h>

// PS/2 controller ports
#define MOUSE_DATA_PORT 0x60
#define MOUSE_STATUS_PORT 0x64

// Simple mouse state
static int mouse_x = 0;
static int mouse_y = 0;
static uint8_t mouse_buttons = 0;
static int packet_index = 0;
static uint8_t packet[3];

// Cursor drawing (simple 8x8 X-shaped cursor)
static void draw_cursor(int x, int y, uint32_t color) {
    framebuffer_info_t info;
    fb_get_info(&info);
    if (info.width == 0) return;
    // draw simple cross
    for (int i = -4; i <= 4; ++i) {
        if (x + i >= 0 && x + i < (int)info.width && y >= 0 && y < (int)info.height) fb_putpixel(x + i, y, color);
        if (y + i >= 0 && y + i < (int)info.height && x >= 0 && x < (int)info.width) fb_putpixel(x, y + i, color);
    }
}

void mouse_init(void) {
    // Unmask IRQ12 on PIC (bit 4 of PIC1? IRQ12 is on PIC2 - need to unmask in PIC2 mapping)
    uint8_t mask1 = inb(0x21);
    uint8_t mask2 = inb(0xA1);
    mask2 &= ~(1 << 4); // clear IRQ12
    outb(0xA1, mask2);
}

// Called from irq handler when IRQ12 occurs
void mouse_handler(void) {
    uint8_t data = inb(MOUSE_DATA_PORT);
    packet[packet_index++] = data;
    if (packet_index == 3) {
        packet_index = 0;
        int8_t dx = (int8_t)packet[1];
        int8_t dy = (int8_t)packet[2];
        // In PS/2, dy is negative when moving up, convert to screen coords
        mouse_x += dx;
        mouse_y -= dy;
        // clamp later
        mouse_buttons = packet[0] & 0x07;

        framebuffer_info_t info;
        fb_get_info(&info);
        if (mouse_x < 0) mouse_x = 0;
        if (mouse_y < 0) mouse_y = 0;
        if (mouse_x >= (int)info.width) mouse_x = info.width - 1;
        if (mouse_y >= (int)info.height) mouse_y = info.height - 1;

        // Draw cursor (very simple, direct draw) - in future we should composite cursor
        draw_cursor(mouse_x, mouse_y, 0x00FFFF00); // yellow
    }
}

void mouse_get_state(int *out_x, int *out_y, uint8_t *out_buttons) {
    if (out_x) *out_x = mouse_x;
    if (out_y) *out_y = mouse_y;
    if (out_buttons) *out_buttons = mouse_buttons;
}

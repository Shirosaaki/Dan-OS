//
// Bitmap Font for Framebuffer Terminal
// 8x16 monospace font covering ASCII 32-127
//
// Each character is 16 bytes (16 rows, 8 pixels per row stored as bits)
// Bit 7 = leftmost pixel, Bit 0 = rightmost pixel
//

#ifndef FONT8X16_H
#define FONT8X16_H

#include <stdint.h>

// Font dimensions
#define FONT8X16_WIDTH  8
#define FONT8X16_HEIGHT 16

// First printable character (space)
#define FONT8X16_FIRST_CHAR 32

// Number of characters in the font (32-127 = 96 characters)
#define FONT8X16_NUM_CHARS 96

// The font bitmap data
// Access: font8x16_data[(char - 32) * 16 + row]
// Each byte represents one row; bit 7 = leftmost pixel
extern const uint8_t font8x16_data[FONT8X16_NUM_CHARS * FONT8X16_HEIGHT];

#endif // FONT8X16_H

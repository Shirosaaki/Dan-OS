#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>

// Keyboard ports
#define KEYBOARD_DATA_PORT 0x60
#define KEYBOARD_STATUS_PORT 0x64

// Initialize the keyboard
void keyboard_init(void);

// Get the last key pressed (returns ASCII character or 0 if none)
char keyboard_getchar(void);

// Check if a key is available
int keyboard_has_key(void);

#endif // KEYBOARD_H

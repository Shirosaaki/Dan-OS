#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>

// Keyboard ports
#define KEYBOARD_DATA_PORT 0x60
#define KEYBOARD_STATUS_PORT 0x64

// Initialize the keyboard (includes numpad support)
void keyboard_init(void);

// Get the last key pressed (returns ASCII character or 0 if none)
char keyboard_getchar(void);

// Check if a key is available
int keyboard_has_key(void);

// Numpad support:
// - Numpad 0-9: numeric input when NumLock is on
// - Numpad +, -, *, /: arithmetic operators
// - Numpad Enter: same as regular Enter key
// - Numpad .: decimal point
// - NumLock toggles between numeric and navigation mode

#endif // KEYBOARD_H

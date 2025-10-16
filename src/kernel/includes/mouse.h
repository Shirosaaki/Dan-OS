#ifndef MOUSE_H
#define MOUSE_H

#include <stdint.h>

// Initialize PS/2 mouse driver
void mouse_init(void);

// IRQ handler called from PIC stub
void mouse_handler(void);

// Get current mouse position and buttons
void mouse_get_state(int *out_x, int *out_y, uint8_t *out_buttons);

#endif // MOUSE_H

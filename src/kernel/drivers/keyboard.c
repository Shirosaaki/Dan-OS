#include "keyboard.h"
#include "tty.h"
#include "../../cpu/ports.h"
#include "../commands/commands.h"

// Scancode to ASCII table (US QWERTY layout)
static const char scancode_to_ascii[] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'a', 'z', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, // Control
    'q', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', 'm', '\'', '`',
    0, // Left shift
    '\\', 'w', 'x', 'c', 'v', 'b', 'n', ';', ',', '.', '/',
    0, // Right shift
    '*',
    0, // Alt
    ' ', // Space
    0, // Caps lock
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // F1-F10
    0, // Num lock
    0, // Scroll lock
    0, // Home
    0, // Up arrow
    0, // Page up
    '-',
    0, // Left arrow
    0,
    0, // Right arrow
    '+',
    0, // End
    0, // Down arrow
    0, // Page down
    0, // Insert
    0, // Delete
    0, 0, 0,
    0, 0 // F11, F12
};

// Scancode to ASCII table with shift (US QWERTY layout)
static const char scancode_to_ascii_shift[] = {
    0,  27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'A', 'Z', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, // Control
    'Q', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', 'M', '"', '~',
    0, // Left shift
    '|', 'W', 'X', 'C', 'V', 'B', 'N', ':', '<', '>', '?',
    0, // Right shift
    '*',
    0, // Alt
    ' ', // Space
    0, // Caps lock
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // F1-F10
    0, // Num lock
    0, // Scroll lock
    0, // Home
    0, // Up arrow
    0, // Page up
    '-',
    0, // Left arrow
    0,
    0, // Right arrow
    '+',
    0, // End
    0, // Down arrow
    0, // Page down
    0, // Insert
    0, // Delete
    0, 0, 0,
    0, 0 // F11, F12
};

// Keyboard state
static int shift_pressed = 0;
static int caps_lock = 0;

// Keyboard buffer
#define KEYBOARD_BUFFER_SIZE 256
static char keyboard_buffer[KEYBOARD_BUFFER_SIZE];
static int buffer_read = 0;
static int buffer_write = 0;

// Initialize the keyboard
void keyboard_init(void) {
    // Clear the keyboard buffer
    buffer_read = 0;
    buffer_write = 0;
    shift_pressed = 0;
    caps_lock = 0;
    
    // Enable keyboard interrupts (unmask IRQ1)
    uint8_t mask = inb(0x21);
    mask &= ~0x02; // Clear bit 1 (IRQ1)
    outb(0x21, mask);
}

// Add character to buffer
static void keyboard_buffer_add(char c) {
    int next = (buffer_write + 1) % KEYBOARD_BUFFER_SIZE;
    if (next != buffer_read) {
        keyboard_buffer[buffer_write] = c;
        buffer_write = next;
    }
}

// Handle keyboard interrupt
void keyboard_handler(void) {
    uint8_t scancode = inb(KEYBOARD_DATA_PORT);
    
    // Check if key is released (scancode has bit 7 set)
    if (scancode & 0x80) {
        scancode &= 0x7F;
        // Handle shift release
        if (scancode == 0x2A || scancode == 0x36) {
            shift_pressed = 0;
        }
        return;
    }
    
    // Handle special keys
    if (scancode == 0x2A || scancode == 0x36) {
        // Left or right shift pressed
        shift_pressed = 1;
        return;
    }
    
    if (scancode == 0x3A) {
        // Caps lock toggle
        caps_lock = !caps_lock;
        return;
    }
    
    // Convert scancode to ASCII
    char c = 0;
    if (scancode < sizeof(scancode_to_ascii)) {
        if (shift_pressed) {
            c = scancode_to_ascii_shift[scancode];
        } else {
            c = scancode_to_ascii[scancode];
            // Apply caps lock to letters
            if (caps_lock && c >= 'a' && c <= 'z') {
                c = c - 'a' + 'A';
            }
        }
        
        if (c != 0) {
            keyboard_buffer_add(c);
            // Echo character to screen and handle special keys
            if (c == '\b') {
                tty_backspace();
            } else if (c == '\n') {
                tty_putchar('\n');
                tty_process_command();
            } else {
                // Regular character - just display it
                tty_putchar(c);
            }
        }
    }
}

// Get character from buffer (non-blocking)
char keyboard_getchar(void) {
    if (buffer_read == buffer_write) {
        return 0; // Buffer empty
    }
    
    char c = keyboard_buffer[buffer_read];
    buffer_read = (buffer_read + 1) % KEYBOARD_BUFFER_SIZE;
    return c;
}

// Check if keyboard has data
int keyboard_has_key(void) {
    return buffer_read != buffer_write;
}

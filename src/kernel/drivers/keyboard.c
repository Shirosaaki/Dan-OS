//
// Created by Shirosaaki on 02/10/2025.
//

#include "keyboard.h"
#include "tty.h"
#include "../../cpu/ports.h"
// framebuffer control
extern void fb_set_visible(int v);
extern int fb_is_visible(void);

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
    '7', // Numpad Home (0x47)
    '8', // Numpad Up arrow (0x48)
    '9', // Numpad Page up (0x49)
    '-', // Numpad minus (0x4A)
    '4', // Numpad Left arrow (0x4B)
    '5', // Numpad center (0x4C)
    '6', // Numpad Right arrow (0x4D)
    '+', // Numpad plus (0x4E)
    '1', // Numpad End (0x4F)
    '2', // Numpad Down arrow (0x50)
    '3', // Numpad Page down (0x51)
    '0', // Numpad Insert (0x52)
    '.', // Numpad Delete (0x53)
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
    '7', // Numpad Home (0x47)
    '8', // Numpad Up arrow (0x48)
    '9', // Numpad Page up (0x49)
    '-', // Numpad minus (0x4A)
    '4', // Numpad Left arrow (0x4B)
    '5', // Numpad center (0x4C)
    '6', // Numpad Right arrow (0x4D)
    '+', // Numpad plus (0x4E)
    '1', // Numpad End (0x4F)
    '2', // Numpad Down arrow (0x50)
    '3', // Numpad Page down (0x51)
    '0', // Numpad Insert (0x52)
    '.', // Numpad Delete (0x53)
    0, 0, 0,
    0, 0 // F11, F12
};

// Keyboard state
static int shift_pressed = 0;
static int caps_lock = 0;
static int ctrl_pressed = 0;
static int num_lock = 1; // Numpad starts enabled
static int extended_scancode = 0; // Flag for 0xE0 prefix

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
    num_lock = 1; // Enable numlock by default
    
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
    char c = 0;
    
    // Check for extended scancode prefix (0xE0)
    if (scancode == 0xE0) {
        extended_scancode = 1;
        return;
    }
    
    // Check if key is released (scancode has bit 7 set)
    if (scancode & 0x80) {
        scancode &= 0x7F;
        extended_scancode = 0; // Reset extended flag
        // Handle key releases
        if (scancode == 0x2A || scancode == 0x36) {
            shift_pressed = 0;
        }
        if (scancode == 0x1D) { // Ctrl released
            ctrl_pressed = 0;
        }
        return;
    }
    
    // Handle special keys
    if (scancode == 0x2A || scancode == 0x36) {
        // Left or right shift pressed
        shift_pressed = 1;
        return;
    }
    
    if (scancode == 0x1D) {
        // Ctrl key pressed
        ctrl_pressed = 1;
        
        // Check if in editor mode - if so, exit editor mode
        #include "tty.h"
        if (tty_is_editor_mode()) {
            tty_exit_editor_mode();
        }
        return;
    }
    
    if (scancode == 0x3A) {
        // Caps lock toggle
        caps_lock = !caps_lock;
        return;
    }

    if (scancode == 0x3B) {
        // F1 pressed - toggle between framebuffer and VGA tty
        if (fb_is_visible()) {
            // hide framebuffer and switch to tty
            fb_set_visible(0);
            // ensure tty is visible and cleared
            tty_clear();
            tty_putstr("DanOS:/$ ");
        } else {
            // show framebuffer
            fb_set_visible(1);
        }
        return;
    }
    
    if (scancode == 0x45) {
        // Num lock toggle
        num_lock = !num_lock;
        return;
    }
    
    // Handle extended scancodes (0xE0 prefix)
    if (extended_scancode) {
        extended_scancode = 0; // Reset flag

        // Arrow keys (E0 48/50/4B/4D)
        if (scancode == 0x48) {
            tty_cursor_up();
            return;
        }
        if (scancode == 0x50) {
            tty_cursor_down();
            return;
        }
        if (scancode == 0x4B) {
            tty_cursor_left();
            return;
        }
        if (scancode == 0x4D) {
            tty_cursor_right();
            return;
        }

        // Numpad Enter (E0 1C)
        if (scancode == 0x1C) {
            c = '\n';
            keyboard_buffer_add(c);

            if (tty_is_editor_mode()) {
                tty_putchar('\n');
                tty_editor_add_char(c);
            } else {
                tty_putchar('\n');
                tty_process_command();
            }
            return;
        }

        // Numpad Division '/' (E0 35)
        if (scancode == 0x35) {
            c = '/';
            keyboard_buffer_add(c);

            if (tty_is_editor_mode()) {
                tty_putchar(c);
                tty_editor_add_char(c);
            } else {
                tty_putchar(c);
            }
            return;
        }

        // Unhandled extended scancode - ignore
        return;
    }
    
    // Note: extended (E0-prefixed) make codes for Numpad Enter and Division
    // are handled above in the extended_scancode branch. The release codes
    // (with 0x80 bit set) are handled in the release path earlier.
    
    // Handle numpad multiply (scancode 0x37)
    if (scancode == 0x37) {
        c = '*';
        keyboard_buffer_add(c);
        
        if (tty_is_editor_mode()) {
            tty_putchar(c);
            tty_editor_add_char(c);
        } else {
            tty_putchar(c);
        }
        return;
    }
    
    // Convert scancode to ASCII
    if (scancode < sizeof(scancode_to_ascii)) {
        // Check if this is a numpad key (scancodes 0x47-0x53)
        if (scancode >= 0x47 && scancode <= 0x53) {
            if (num_lock) {
                // Num lock is on - use numeric values
                c = scancode_to_ascii[scancode];
            } else {
                // Num lock is off - numpad acts as navigation keys
                // For now, we'll just ignore these when num lock is off
                c = 0;
            }
        } else {
            // Regular key handling
            if (shift_pressed) {
                c = scancode_to_ascii_shift[scancode];
            } else {
                c = scancode_to_ascii[scancode];
                // Apply caps lock to letters
                if (caps_lock && c >= 'a' && c <= 'z') {
                    c = c - 'a' + 'A';
                }
            }
        }
        
        if (c != 0) {
            keyboard_buffer_add(c);
            
            // Check if in editor mode
            #include "tty.h"
            if (tty_is_editor_mode()) {
                // In editor mode - handle differently
                if (c == '\b') {
                    tty_editor_backspace();
                    // Need to redraw from cursor position
                    tty_editor_redraw();
                } else if (c == '\n') {
                    tty_putchar('\n');
                    tty_editor_add_char(c);
                } else {
                    // Regular character in editor - insert at cursor
                    tty_editor_add_char(c);
                    // Redraw from cursor position
                    tty_editor_redraw();
                }
            } else {
                // Normal mode - handle commands
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

//
// Created by Shirosaaki on 02/10/2025.
//

#include "keyboard.h"
#include "tty.h"
#include "../../cpu/ports.h"

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
// When capture_mode is non-zero, the keyboard handler will NOT forward keys to TTY
static int capture_mode = 0;

// Initialize the keyboard
void keyboard_init(void) {
    // Clear the keyboard buffer
    buffer_read = 0;
    buffer_write = 0;
    shift_pressed = 0;
    caps_lock = 0;
    num_lock = 1; // Enable numlock by default
    
    // Wait for keyboard controller to be ready
    while (inb(KEYBOARD_STATUS_PORT) & 0x02);
    
    // Disable devices during initialization
    outb(KEYBOARD_STATUS_PORT, 0xAD); // Disable first PS/2 port
    outb(KEYBOARD_STATUS_PORT, 0xA7); // Disable second PS/2 port (if exists)
    
    // Flush the output buffer
    while (inb(KEYBOARD_STATUS_PORT) & 0x01) {
        inb(KEYBOARD_DATA_PORT);
    }
    
    // Read controller configuration byte
    while (inb(KEYBOARD_STATUS_PORT) & 0x02);
    outb(KEYBOARD_STATUS_PORT, 0x20);
    while (!(inb(KEYBOARD_STATUS_PORT) & 0x01));
    uint8_t config = inb(KEYBOARD_DATA_PORT);
    
    // Enable IRQ1 (keyboard interrupt) and translation
    config |= 0x01;  // Enable IRQ1
    config |= 0x40;  // Enable translation (scancode set 1)
    config &= ~0x10; // Enable keyboard clock
    
    // Write back configuration
    while (inb(KEYBOARD_STATUS_PORT) & 0x02);
    outb(KEYBOARD_STATUS_PORT, 0x60);
    while (inb(KEYBOARD_STATUS_PORT) & 0x02);
    outb(KEYBOARD_DATA_PORT, config);
    
    // Perform controller self-test
    while (inb(KEYBOARD_STATUS_PORT) & 0x02);
    outb(KEYBOARD_STATUS_PORT, 0xAA);
    while (!(inb(KEYBOARD_STATUS_PORT) & 0x01));
    uint8_t test_result = inb(KEYBOARD_DATA_PORT);
    if (test_result != 0x55) {
        // Controller self-test failed, but continue anyway
        // Some controllers don't respond properly
    }
    
    // Test first PS/2 port
    while (inb(KEYBOARD_STATUS_PORT) & 0x02);
    outb(KEYBOARD_STATUS_PORT, 0xAB);
    while (!(inb(KEYBOARD_STATUS_PORT) & 0x01));
    inb(KEYBOARD_DATA_PORT); // Read and discard result
    
    // Re-enable first PS/2 port
    while (inb(KEYBOARD_STATUS_PORT) & 0x02);
    outb(KEYBOARD_STATUS_PORT, 0xAE);
    
    // Reset keyboard device
    while (inb(KEYBOARD_STATUS_PORT) & 0x02);
    outb(KEYBOARD_DATA_PORT, 0xFF);
    
    // Wait for ACK (0xFA) and self-test result (0xAA)
    // Some keyboards may not respond, so add timeout
    for (int timeout = 0; timeout < 100000; timeout++) {
        if (inb(KEYBOARD_STATUS_PORT) & 0x01) {
            uint8_t response = inb(KEYBOARD_DATA_PORT);
            if (response == 0xAA) break; // Self-test passed
            if (response == 0xFC || response == 0xFD) break; // Self-test failed
        }
    }
    
    // Enable scanning (keyboard will start sending scancodes)
    while (inb(KEYBOARD_STATUS_PORT) & 0x02);
    outb(KEYBOARD_DATA_PORT, 0xF4);
    
    // Wait for ACK
    for (int timeout = 0; timeout < 100000; timeout++) {
        if (inb(KEYBOARD_STATUS_PORT) & 0x01) {
            inb(KEYBOARD_DATA_PORT);
            break;
        }
    }
    
    // Enable keyboard interrupts (unmask IRQ1) in PIC
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
        return;
    }
    
    // Handle Ctrl+S to save in editor mode
    if (ctrl_pressed && (scancode == 0x1F)) { // 's' key scancode
        if (tty_is_editor_mode()) {
            tty_editor_save();
        }
        return;
    }
    
    // Handle Ctrl+E to exit editor mode (without saving)
    if (ctrl_pressed && (scancode == 0x12)) { // 'e' key scancode
        if (tty_is_editor_mode()) {
            tty_exit_editor_mode();
        }
        return;
    }
    
    // Handle Ctrl+D to stop running program / signal EOF
    if (ctrl_pressed && (scancode == 0x20)) { // 'd' key scancode
        // Signal to stop current process
        tty_signal_stop();
        return;
    }
    
    // Handle Ctrl+Space to toggle selection mode
    if (ctrl_pressed && (scancode == 0x39)) { // Space key scancode
        if (tty_is_selecting()) {
            tty_cancel_selection();  // Cancel if already selecting
        } else {
            tty_start_selection();   // Start selection
        }
        return;
    }
    
    // Handle Ctrl+C to copy
    if (ctrl_pressed && (scancode == 0x2E)) { // 'c' key scancode
        tty_copy();
        return;
    }
    
    // Handle Ctrl+V to paste
    if (ctrl_pressed && (scancode == 0x2F)) { // 'v' key scancode
        tty_paste();
        return;
    }
    
    if (scancode == 0x3A) {
        // Caps lock toggle
        caps_lock = !caps_lock;
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
        int forward = !capture_mode; // if capture_mode is enabled, do not forward to TTY

        // Arrow keys (E0 48/50/4B/4D)
        if (scancode == 0x48) {
            // Up arrow - show previous command in history (when not in editor)
            if (forward) {
                if (!tty_is_editor_mode()) {
                    tty_history_up();
                } else {
                    tty_cursor_up();
                }
            } else {
                // still buffer the key for the VM
                keyboard_buffer_add(0); // placeholder - VM expects arrow via extended handling
            }
            return;
        }
        if (scancode == 0x50) {
            if (forward) {
                if (!tty_is_editor_mode()) {
                    tty_history_down();
                } else {
                    tty_cursor_down();
                }
            } else {
                keyboard_buffer_add(0);
            }
            return;
        }
        if (scancode == 0x4B) {
            if (forward) tty_cursor_left(); else keyboard_buffer_add(0);
            return;
        }
        if (scancode == 0x4D) {
            if (forward) tty_cursor_right(); else keyboard_buffer_add(0);
            return;
        }

        // Numpad Enter (E0 1C)
        if (scancode == 0x1C) {
            c = '\n';
            keyboard_buffer_add(c);
            if (!forward) return;

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
            if (!forward) return;

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
            // If capture mode is enabled, do not forward to TTY/command handling
            if (capture_mode) return;
            
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

// Peek next key without consuming it
char keyboard_peek(void) {
    if (buffer_read == buffer_write) return 0;
    return keyboard_buffer[buffer_read];
}

// Consume one key (after peek)
void keyboard_consume(void) {
    if (buffer_read == buffer_write) return;
    buffer_read = (buffer_read + 1) % KEYBOARD_BUFFER_SIZE;
}

// Capture functions
void keyboard_set_capture(int enable) {
    capture_mode = enable ? 1 : 0;
}

int keyboard_get_capture(void) {
    return capture_mode;
}

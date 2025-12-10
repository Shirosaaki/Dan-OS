//
// USB Core and HID Keyboard Driver Implementation
//

#include "../includes/usb.h"
#include "../includes/pci.h"
#include "../includes/uhci.h"
#include "../includes/ehci.h"
#include "../includes/xhci.h"
#include "../includes/tty.h"
#include "../includes/kmalloc.h"

// USB Controllers
#define MAX_USB_CONTROLLERS 8
static usb_controller_t usb_controllers[MAX_USB_CONTROLLERS];
static int num_usb_controllers = 0;

// USB Drivers
#define MAX_USB_DRIVERS 16
static usb_driver_t* usb_drivers[MAX_USB_DRIVERS];
static int num_usb_drivers = 0;

// USB Keyboard state
static usb_device_t* usb_keyboard_device = 0;
static uint8_t usb_keyboard_endpoint = 0;
static uint8_t usb_keyboard_report[8];
static uint8_t usb_keyboard_prev_report[8];
static int usb_keyboard_initialized = 0;

// Keyboard buffer for USB keyboard
#define USB_KB_BUFFER_SIZE 64
static char usb_kb_buffer[USB_KB_BUFFER_SIZE];
static int usb_kb_buffer_read = 0;
static int usb_kb_buffer_write = 0;

// Modifier keys
static int usb_shift_pressed = 0;
static int usb_ctrl_pressed = 0;
static int usb_alt_pressed = 0;
static int usb_caps_lock = 0;

// HID Usage to ASCII mapping (US keyboard layout)
static const char hid_to_ascii[128] = {
    0,   0,   0,   0,   'a', 'b', 'c', 'd',  // 0x00-0x07
    'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l',  // 0x08-0x0F
    'm', 'n', 'o', 'p', 'q', 'r', 's', 't',  // 0x10-0x17
    'u', 'v', 'w', 'x', 'y', 'z', '1', '2',  // 0x18-0x1F
    '3', '4', '5', '6', '7', '8', '9', '0',  // 0x20-0x27
    '\n', 27,  '\b', '\t', ' ', '-', '=', '[',  // 0x28-0x2F
    ']', '\\', 0,   ';', '\'', '`', ',', '.',  // 0x30-0x37
    '/',  0,   0,   0,   0,   0,   0,   0,   // 0x38-0x3F (caps, F1-F6)
    0,   0,   0,   0,   0,   0,   0,   0,   // 0x40-0x47 (F7-F12, etc)
    0,   0,   0,   0,   0,   0,   0,   0,   // 0x48-0x4F (arrows, etc)
    0,   0,   0,   0,   '/', '*', '-', '+',  // 0x50-0x57 (numpad)
    '\n', '1', '2', '3', '4', '5', '6', '7',  // 0x58-0x5F (numpad)
    '8', '9', '0', '.', 0,   0,   0,   0,   // 0x60-0x67
    0,   0,   0,   0,   0,   0,   0,   0,   // 0x68-0x6F
    0,   0,   0,   0,   0,   0,   0,   0,   // 0x70-0x77
    0,   0,   0,   0,   0,   0,   0,   0    // 0x78-0x7F
};

// HID Usage to ASCII with shift
static const char hid_to_ascii_shift[128] = {
    0,   0,   0,   0,   'A', 'B', 'C', 'D',  // 0x00-0x07
    'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L',  // 0x08-0x0F
    'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T',  // 0x10-0x17
    'U', 'V', 'W', 'X', 'Y', 'Z', '!', '@',  // 0x18-0x1F
    '#', '$', '%', '^', '&', '*', '(', ')',  // 0x20-0x27
    '\n', 27,  '\b', '\t', ' ', '_', '+', '{',  // 0x28-0x2F
    '}', '|', 0,   ':', '"', '~', '<', '>',  // 0x30-0x37
    '?',  0,   0,   0,   0,   0,   0,   0,   // 0x38-0x3F
    0,   0,   0,   0,   0,   0,   0,   0,   // 0x40-0x47
    0,   0,   0,   0,   0,   0,   0,   0,   // 0x48-0x4F
    0,   0,   0,   0,   '/', '*', '-', '+',  // 0x50-0x57
    '\n', '1', '2', '3', '4', '5', '6', '7',  // 0x58-0x5F
    '8', '9', '0', '.', 0,   0,   0,   0,   // 0x60-0x67
    0,   0,   0,   0,   0,   0,   0,   0,   // 0x68-0x6F
    0,   0,   0,   0,   0,   0,   0,   0,   // 0x70-0x77
    0,   0,   0,   0,   0,   0,   0,   0    // 0x78-0x7F
};

// HID modifier bits
#define HID_MOD_LEFT_CTRL   0x01
#define HID_MOD_LEFT_SHIFT  0x02
#define HID_MOD_LEFT_ALT    0x04
#define HID_MOD_LEFT_GUI    0x08
#define HID_MOD_RIGHT_CTRL  0x10
#define HID_MOD_RIGHT_SHIFT 0x20
#define HID_MOD_RIGHT_ALT   0x40
#define HID_MOD_RIGHT_GUI   0x80

// HID special keys
#define HID_KEY_CAPS_LOCK   0x39

// Delay helper
static void usb_delay(int ms) {
    for (volatile int i = 0; i < ms * 10000; i++) {
        __asm__ volatile("nop");
    }
}

// Add character to USB keyboard buffer
static void usb_kb_buffer_add(char c) {
    int next = (usb_kb_buffer_write + 1) % USB_KB_BUFFER_SIZE;
    if (next != usb_kb_buffer_read) {
        usb_kb_buffer[usb_kb_buffer_write] = c;
        usb_kb_buffer_write = next;
    }
}

// Process USB keyboard report
static void usb_keyboard_process_report(uint8_t* report) {
    uint8_t modifiers = report[0];
    
    // Update modifier state
    usb_shift_pressed = (modifiers & (HID_MOD_LEFT_SHIFT | HID_MOD_RIGHT_SHIFT)) != 0;
    usb_ctrl_pressed = (modifiers & (HID_MOD_LEFT_CTRL | HID_MOD_RIGHT_CTRL)) != 0;
    usb_alt_pressed = (modifiers & (HID_MOD_LEFT_ALT | HID_MOD_RIGHT_ALT)) != 0;
    
    // Process key presses (report bytes 2-7)
    for (int i = 2; i < 8; i++) {
        uint8_t key = report[i];
        if (key == 0 || key == 1) continue; // No key or error
        
        // Check if this key was not in the previous report (new press)
        int was_pressed = 0;
        for (int j = 2; j < 8; j++) {
            if (usb_keyboard_prev_report[j] == key) {
                was_pressed = 1;
                break;
            }
        }
        
        if (!was_pressed) {
            // New key press
            if (key == HID_KEY_CAPS_LOCK) {
                usb_caps_lock = !usb_caps_lock;
            } else if (key < 128) {
                char c;
                if (usb_shift_pressed) {
                    c = hid_to_ascii_shift[key];
                } else {
                    c = hid_to_ascii[key];
                }
                
                // Apply caps lock to letters
                if (usb_caps_lock && c >= 'a' && c <= 'z') {
                    c = c - 'a' + 'A';
                } else if (usb_caps_lock && c >= 'A' && c <= 'Z') {
                    c = c - 'A' + 'a';
                }
                
                if (c != 0) {
                    usb_kb_buffer_add(c);
                    
                    // Also send to TTY for immediate display
                    extern void tty_putchar(char c);
                    extern void tty_backspace(void);
                    extern void tty_process_command(void);
                    extern int tty_is_editor_mode(void);
                    
                    if (c == '\b') {
                        tty_backspace();
                    } else if (c == '\n') {
                        tty_putchar('\n');
                        if (!tty_is_editor_mode()) {
                            tty_process_command();
                        }
                    } else {
                        tty_putchar(c);
                    }
                }
            }
        }
    }
    
    // Save current report for next comparison
    for (int i = 0; i < 8; i++) {
        usb_keyboard_prev_report[i] = report[i];
    }
}

// USB control transfer wrapper
int usb_control_transfer(usb_device_t* dev, uint8_t request_type, uint8_t request,
                         uint16_t value, uint16_t index, void* data, uint16_t length) {
    usb_setup_packet_t setup;
    setup.bmRequestType = request_type;
    setup.bRequest = request;
    setup.wValue = value;
    setup.wIndex = index;
    setup.wLength = length;
    
    return dev->controller->ops->control_transfer(dev, &setup, data, length);
}

// Get descriptor
int usb_get_descriptor(usb_device_t* dev, uint8_t type, uint8_t index, void* data, uint16_t length) {
    return usb_control_transfer(dev, USB_REQ_DIR_IN | USB_REQ_TYPE_STANDARD | USB_REQ_RECIP_DEVICE,
                                USB_REQ_GET_DESCRIPTOR, (type << 8) | index, 0, data, length);
}

// Set configuration
int usb_set_configuration(usb_device_t* dev, uint8_t config) {
    return usb_control_transfer(dev, USB_REQ_DIR_OUT | USB_REQ_TYPE_STANDARD | USB_REQ_RECIP_DEVICE,
                                USB_REQ_SET_CONFIGURATION, config, 0, 0, 0);
}

// Set interface
int usb_set_interface(usb_device_t* dev, uint8_t interface, uint8_t alt_setting) {
    return usb_control_transfer(dev, USB_REQ_DIR_OUT | USB_REQ_TYPE_STANDARD | USB_REQ_RECIP_INTERFACE,
                                USB_REQ_SET_INTERFACE, alt_setting, interface, 0, 0);
}

// Set protocol (for HID)
static int usb_hid_set_protocol(usb_device_t* dev, uint8_t interface, uint8_t protocol) {
    return usb_control_transfer(dev, USB_REQ_DIR_OUT | USB_REQ_TYPE_CLASS | USB_REQ_RECIP_INTERFACE,
                                USB_HID_SET_PROTOCOL, protocol, interface, 0, 0);
}

// Set idle (for HID)
static int usb_hid_set_idle(usb_device_t* dev, uint8_t interface, uint8_t duration, uint8_t report_id) {
    return usb_control_transfer(dev, USB_REQ_DIR_OUT | USB_REQ_TYPE_CLASS | USB_REQ_RECIP_INTERFACE,
                                USB_HID_SET_IDLE, (duration << 8) | report_id, interface, 0, 0);
}

// Probe for HID keyboard
static int usb_keyboard_probe(usb_device_t* dev) {
    // Check if already have a keyboard
    if (usb_keyboard_initialized) {
        return -1;
    }
    
    // Get configuration descriptor
    uint8_t config_buf[256];
    if (usb_get_descriptor(dev, USB_DESC_CONFIGURATION, 0, config_buf, sizeof(config_buf)) != USB_TRANSFER_SUCCESS) {
        return -1;
    }
    
    usb_config_descriptor_t* config = (usb_config_descriptor_t*)config_buf;
    
    // Parse descriptors to find HID keyboard interface
    uint8_t* ptr = config_buf + config->bLength;
    uint8_t* end = config_buf + config->wTotalLength;
    
    int interface_num = -1;
    int is_keyboard = 0;
    uint8_t endpoint_addr = 0;
    
    while (ptr < end) {
        uint8_t len = ptr[0];
        uint8_t type = ptr[1];
        
        if (len == 0) break;
        
        if (type == USB_DESC_INTERFACE) {
            usb_interface_descriptor_t* iface = (usb_interface_descriptor_t*)ptr;
            
            if (iface->bInterfaceClass == USB_CLASS_HID &&
                iface->bInterfaceSubClass == USB_HID_SUBCLASS_BOOT &&
                iface->bInterfaceProtocol == USB_HID_PROTOCOL_KEYBOARD) {
                interface_num = iface->bInterfaceNumber;
                is_keyboard = 1;
            } else {
                is_keyboard = 0;
            }
        } else if (type == USB_DESC_ENDPOINT && is_keyboard) {
            usb_endpoint_descriptor_t* ep = (usb_endpoint_descriptor_t*)ptr;
            
            if ((ep->bmAttributes & 0x03) == USB_ENDPOINT_INTERRUPT &&
                (ep->bEndpointAddress & USB_ENDPOINT_DIR_IN)) {
                endpoint_addr = ep->bEndpointAddress;
                
                // Setup endpoint in device
                int ep_num = endpoint_addr & 0x0F;
                dev->endpoints[ep_num].address = endpoint_addr;
                dev->endpoints[ep_num].type = USB_ENDPOINT_INTERRUPT;
                dev->endpoints[ep_num].max_packet_size = ep->wMaxPacketSize;
                dev->endpoints[ep_num].interval = ep->bInterval;
                dev->endpoints[ep_num].toggle = 0;
                dev->num_endpoints++;
            }
        }
        
        ptr += len;
    }
    
    if (interface_num < 0 || endpoint_addr == 0) {
        return -1;
    }
    
    tty_putstr("USB: Found keyboard on interface ");
    tty_putdec(interface_num);
    tty_putstr("\n");
    
    // Set configuration
    if (usb_set_configuration(dev, config->bConfigurationValue) != USB_TRANSFER_SUCCESS) {
        tty_putstr("USB: Failed to set configuration\n");
        return -1;
    }
    
    // Set boot protocol
    usb_hid_set_protocol(dev, interface_num, 0); // 0 = boot protocol
    
    // Set idle (0 = report only on change)
    usb_hid_set_idle(dev, interface_num, 0, 0);
    
    // Store keyboard info
    usb_keyboard_device = dev;
    usb_keyboard_endpoint = endpoint_addr;
    usb_keyboard_initialized = 1;
    
    // Clear previous report
    for (int i = 0; i < 8; i++) {
        usb_keyboard_prev_report[i] = 0;
    }
    
    tty_putstr("USB: Keyboard initialized\n");
    
    return 0;
}

// USB keyboard driver
static usb_driver_t usb_keyboard_driver = {
    .name = "USB Keyboard",
    .probe = usb_keyboard_probe,
    .disconnect = 0
};

// Enumerate a new USB device
static void usb_enumerate_device(usb_controller_t* ctrl, int port, int speed) {
    // Allocate device structure
    usb_device_t* dev = (usb_device_t*)kmalloc(sizeof(usb_device_t));
    if (!dev) return;
    
    // Initialize device
    for (int i = 0; i < (int)sizeof(usb_device_t); i++) {
        ((uint8_t*)dev)[i] = 0;
    }
    
    dev->controller = ctrl;
    dev->port = port;
    dev->speed = speed;
    dev->address = 0;
    dev->max_packet_size = (speed == USB_SPEED_LOW) ? 8 : 64;
    dev->state = USB_STATE_DEFAULT;
    
    // Get device descriptor (first 8 bytes)
    usb_device_descriptor_t desc;
    if (usb_get_descriptor(dev, USB_DESC_DEVICE, 0, &desc, 8) != USB_TRANSFER_SUCCESS) {
        tty_putstr("USB: Failed to get device descriptor\n");
        kfree(dev);
        return;
    }
    
    dev->max_packet_size = desc.bMaxPacketSize0;
    
    // Assign address
    int address = ctrl->next_address++;
    if (address > 127) address = 1;
    
    if (usb_control_transfer(dev, USB_REQ_DIR_OUT | USB_REQ_TYPE_STANDARD | USB_REQ_RECIP_DEVICE,
                             USB_REQ_SET_ADDRESS, address, 0, 0, 0) != USB_TRANSFER_SUCCESS) {
        tty_putstr("USB: Failed to set address\n");
        kfree(dev);
        return;
    }
    
    usb_delay(10);
    dev->address = address;
    dev->state = USB_STATE_ADDRESS;
    
    // Get full device descriptor
    if (usb_get_descriptor(dev, USB_DESC_DEVICE, 0, &dev->device_desc, 18) != USB_TRANSFER_SUCCESS) {
        tty_putstr("USB: Failed to get full device descriptor\n");
        kfree(dev);
        return;
    }
    
    tty_putstr("USB: Device ");
    tty_puthex(dev->device_desc.idVendor);
    tty_putstr(":");
    tty_puthex(dev->device_desc.idProduct);
    tty_putstr(" at address ");
    tty_putdec(address);
    tty_putstr("\n");
    
    // Store device
    for (int i = 0; i < USB_MAX_DEVICES; i++) {
        if (!ctrl->devices[i]) {
            ctrl->devices[i] = dev;
            ctrl->num_devices++;
            break;
        }
    }
    
    // Try drivers
    for (int i = 0; i < num_usb_drivers; i++) {
        if (usb_drivers[i] && usb_drivers[i]->probe) {
            if (usb_drivers[i]->probe(dev) == 0) {
                dev->state = USB_STATE_CONFIGURED;
                break;
            }
        }
    }
}

// Scan for USB devices on a controller
static void usb_scan_controller(usb_controller_t* ctrl) {
    // This will be called during polling when a device is connected
}

// Initialize USB controller from PCI device
static int usb_init_controller(pci_device_t* pci_dev) {
    if (num_usb_controllers >= MAX_USB_CONTROLLERS) {
        return -1;
    }
    
    usb_controller_t* ctrl = &usb_controllers[num_usb_controllers];
    ctrl->next_address = 1;
    
    int result = -1;
    
    switch (pci_dev->prog_if) {
        case PCI_USB_PROG_IF_UHCI:
            tty_putstr("USB: Initializing UHCI controller\n");
            result = uhci_init(ctrl, pci_dev);
            break;
            
        case PCI_USB_PROG_IF_EHCI:
            tty_putstr("USB: Initializing EHCI controller\n");
            result = ehci_init(ctrl, pci_dev);
            break;
            
        case PCI_USB_PROG_IF_XHCI:
            tty_putstr("USB: Initializing XHCI controller\n");
            result = xhci_init(ctrl, pci_dev);
            break;
            
        default:
            tty_putstr("USB: Unknown controller type: 0x");
            tty_puthex(pci_dev->prog_if);
            tty_putstr("\n");
            return -1;
    }
    
    if (result == 0) {
        num_usb_controllers++;
    }
    
    return result;
}

// Initialize USB subsystem
void usb_init(void) {
    tty_putstr("USB: Initializing USB subsystem\n");
    
    num_usb_controllers = 0;
    num_usb_drivers = 0;
    
    // Initialize PCI
    pci_init();
    
    // Find USB controllers
    pci_device_t* pci_dev = 0;
    
    // Find XHCI controllers first (USB 3.0)
    pci_dev = pci_find_class(PCI_CLASS_SERIAL_BUS, PCI_SUBCLASS_USB, PCI_USB_PROG_IF_XHCI);
    while (pci_dev) {
        usb_init_controller(pci_dev);
        pci_dev = pci_find_next_class(PCI_CLASS_SERIAL_BUS, PCI_SUBCLASS_USB, PCI_USB_PROG_IF_XHCI, pci_dev);
    }
    
    // Find EHCI controllers (USB 2.0)
    pci_dev = pci_find_class(PCI_CLASS_SERIAL_BUS, PCI_SUBCLASS_USB, PCI_USB_PROG_IF_EHCI);
    while (pci_dev) {
        usb_init_controller(pci_dev);
        pci_dev = pci_find_next_class(PCI_CLASS_SERIAL_BUS, PCI_SUBCLASS_USB, PCI_USB_PROG_IF_EHCI, pci_dev);
    }
    
    // Find UHCI controllers (USB 1.1)
    pci_dev = pci_find_class(PCI_CLASS_SERIAL_BUS, PCI_SUBCLASS_USB, PCI_USB_PROG_IF_UHCI);
    while (pci_dev) {
        usb_init_controller(pci_dev);
        pci_dev = pci_find_next_class(PCI_CLASS_SERIAL_BUS, PCI_SUBCLASS_USB, PCI_USB_PROG_IF_UHCI, pci_dev);
    }
    
    // Register keyboard driver
    usb_register_driver(&usb_keyboard_driver);
    
    tty_putstr("USB: Found ");
    tty_putdec(num_usb_controllers);
    tty_putstr(" USB controller(s)\n");
    
    // Initial scan for connected devices
    for (int i = 0; i < num_usb_controllers; i++) {
        if (usb_controllers[i].ops && usb_controllers[i].ops->poll) {
            usb_controllers[i].ops->poll(&usb_controllers[i]);
        }
    }
}

// Poll all USB controllers
void usb_poll(void) {
    // Poll controllers for port changes
    for (int i = 0; i < num_usb_controllers; i++) {
        if (usb_controllers[i].ops && usb_controllers[i].ops->poll) {
            usb_controllers[i].ops->poll(&usb_controllers[i]);
        }
    }
    
    // Poll keyboard if initialized
    if (usb_keyboard_initialized && usb_keyboard_device) {
        usb_controller_t* ctrl = usb_keyboard_device->controller;
        
        if (ctrl && ctrl->ops && ctrl->ops->interrupt_transfer) {
            int result = ctrl->ops->interrupt_transfer(usb_keyboard_device,
                                                       usb_keyboard_endpoint,
                                                       usb_keyboard_report, 8);
            
            if (result == USB_TRANSFER_SUCCESS) {
                usb_keyboard_process_report(usb_keyboard_report);
            }
        }
    }
}

// Register USB driver
void usb_register_driver(usb_driver_t* driver) {
    if (num_usb_drivers < MAX_USB_DRIVERS) {
        usb_drivers[num_usb_drivers++] = driver;
    }
}

// USB keyboard public interface
int usb_keyboard_poll(void) {
    usb_poll();
    return usb_keyboard_initialized;
}

char usb_keyboard_getchar(void) {
    if (usb_kb_buffer_read == usb_kb_buffer_write) {
        return 0;
    }
    
    char c = usb_kb_buffer[usb_kb_buffer_read];
    usb_kb_buffer_read = (usb_kb_buffer_read + 1) % USB_KB_BUFFER_SIZE;
    return c;
}

int usb_keyboard_has_key(void) {
    return usb_kb_buffer_read != usb_kb_buffer_write;
}

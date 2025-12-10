//
// USB Core Header
//

#ifndef USB_H
#define USB_H

#include <stdint.h>

// USB Speed
#define USB_SPEED_LOW       0   // 1.5 Mbps
#define USB_SPEED_FULL      1   // 12 Mbps
#define USB_SPEED_HIGH      2   // 480 Mbps
#define USB_SPEED_SUPER     3   // 5 Gbps

// USB Request Types
#define USB_REQ_TYPE_STANDARD   0x00
#define USB_REQ_TYPE_CLASS      0x20
#define USB_REQ_TYPE_VENDOR     0x40

#define USB_REQ_DIR_OUT         0x00
#define USB_REQ_DIR_IN          0x80

#define USB_REQ_RECIP_DEVICE    0x00
#define USB_REQ_RECIP_INTERFACE 0x01
#define USB_REQ_RECIP_ENDPOINT  0x02
#define USB_REQ_RECIP_OTHER     0x03

// Standard USB Requests
#define USB_REQ_GET_STATUS          0x00
#define USB_REQ_CLEAR_FEATURE       0x01
#define USB_REQ_SET_FEATURE         0x03
#define USB_REQ_SET_ADDRESS         0x05
#define USB_REQ_GET_DESCRIPTOR      0x06
#define USB_REQ_SET_DESCRIPTOR      0x07
#define USB_REQ_GET_CONFIGURATION   0x08
#define USB_REQ_SET_CONFIGURATION   0x09
#define USB_REQ_GET_INTERFACE       0x0A
#define USB_REQ_SET_INTERFACE       0x0B
#define USB_REQ_SYNCH_FRAME         0x0C

// Descriptor Types
#define USB_DESC_DEVICE         0x01
#define USB_DESC_CONFIGURATION  0x02
#define USB_DESC_STRING         0x03
#define USB_DESC_INTERFACE      0x04
#define USB_DESC_ENDPOINT       0x05
#define USB_DESC_HID            0x21
#define USB_DESC_HID_REPORT     0x22

// USB Class Codes
#define USB_CLASS_HID           0x03
#define USB_CLASS_MASS_STORAGE  0x08
#define USB_CLASS_HUB           0x09

// HID Subclass Codes
#define USB_HID_SUBCLASS_NONE   0x00
#define USB_HID_SUBCLASS_BOOT   0x01

// HID Protocol Codes
#define USB_HID_PROTOCOL_NONE       0x00
#define USB_HID_PROTOCOL_KEYBOARD   0x01
#define USB_HID_PROTOCOL_MOUSE      0x02

// HID Class-Specific Requests
#define USB_HID_GET_REPORT      0x01
#define USB_HID_GET_IDLE        0x02
#define USB_HID_GET_PROTOCOL    0x03
#define USB_HID_SET_REPORT      0x09
#define USB_HID_SET_IDLE        0x0A
#define USB_HID_SET_PROTOCOL    0x0B

// Endpoint Types
#define USB_ENDPOINT_CONTROL        0x00
#define USB_ENDPOINT_ISOCHRONOUS    0x01
#define USB_ENDPOINT_BULK           0x02
#define USB_ENDPOINT_INTERRUPT      0x03

// Transfer direction (from endpoint address)
#define USB_ENDPOINT_DIR_OUT    0x00
#define USB_ENDPOINT_DIR_IN     0x80

// USB Device Descriptor (18 bytes)
typedef struct __attribute__((packed)) {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t bcdUSB;
    uint8_t  bDeviceClass;
    uint8_t  bDeviceSubClass;
    uint8_t  bDeviceProtocol;
    uint8_t  bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t  iManufacturer;
    uint8_t  iProduct;
    uint8_t  iSerialNumber;
    uint8_t  bNumConfigurations;
} usb_device_descriptor_t;

// USB Configuration Descriptor (9 bytes)
typedef struct __attribute__((packed)) {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t wTotalLength;
    uint8_t  bNumInterfaces;
    uint8_t  bConfigurationValue;
    uint8_t  iConfiguration;
    uint8_t  bmAttributes;
    uint8_t  bMaxPower;
} usb_config_descriptor_t;

// USB Interface Descriptor (9 bytes)
typedef struct __attribute__((packed)) {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bInterfaceNumber;
    uint8_t  bAlternateSetting;
    uint8_t  bNumEndpoints;
    uint8_t  bInterfaceClass;
    uint8_t  bInterfaceSubClass;
    uint8_t  bInterfaceProtocol;
    uint8_t  iInterface;
} usb_interface_descriptor_t;

// USB Endpoint Descriptor (7 bytes)
typedef struct __attribute__((packed)) {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bEndpointAddress;
    uint8_t  bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t  bInterval;
} usb_endpoint_descriptor_t;

// USB HID Descriptor
typedef struct __attribute__((packed)) {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t bcdHID;
    uint8_t  bCountryCode;
    uint8_t  bNumDescriptors;
    uint8_t  bReportDescriptorType;
    uint16_t wReportDescriptorLength;
} usb_hid_descriptor_t;

// USB Setup Packet (8 bytes)
typedef struct __attribute__((packed)) {
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} usb_setup_packet_t;

// Maximum number of USB devices
#define USB_MAX_DEVICES 32

// USB Transfer completion status
#define USB_TRANSFER_SUCCESS    0
#define USB_TRANSFER_STALL      1
#define USB_TRANSFER_ERROR      2
#define USB_TRANSFER_TIMEOUT    3
#define USB_TRANSFER_NAK        4

// USB Device State
typedef enum {
    USB_STATE_DETACHED,
    USB_STATE_ATTACHED,
    USB_STATE_POWERED,
    USB_STATE_DEFAULT,
    USB_STATE_ADDRESS,
    USB_STATE_CONFIGURED,
    USB_STATE_SUSPENDED
} usb_device_state_t;

// Forward declarations
struct usb_device;
struct usb_controller;

// USB Transfer structure
typedef struct usb_transfer {
    struct usb_device* device;
    uint8_t endpoint;
    uint8_t type;           // Control, Bulk, Interrupt, Isochronous
    uint8_t direction;      // IN or OUT
    void* data;
    uint32_t length;
    uint32_t actual_length;
    int status;
    void (*callback)(struct usb_transfer*);
    void* user_data;
} usb_transfer_t;

// USB Endpoint structure
typedef struct {
    uint8_t address;
    uint8_t type;
    uint16_t max_packet_size;
    uint8_t interval;
    uint8_t toggle;         // Data toggle bit
} usb_endpoint_t;

// USB Device structure
typedef struct usb_device {
    struct usb_controller* controller;
    uint8_t address;
    uint8_t port;
    uint8_t speed;
    uint8_t max_packet_size;
    usb_device_state_t state;
    
    usb_device_descriptor_t device_desc;
    usb_config_descriptor_t config_desc;
    
    usb_endpoint_t endpoints[16];
    int num_endpoints;
    
    // Driver data
    void* driver_data;
    
    // Hub device (if connected through hub)
    struct usb_device* hub;
} usb_device_t;

// USB Controller operations
typedef struct {
    int (*init)(struct usb_controller* ctrl);
    void (*shutdown)(struct usb_controller* ctrl);
    int (*reset_port)(struct usb_controller* ctrl, int port);
    int (*control_transfer)(usb_device_t* dev, usb_setup_packet_t* setup, void* data, uint16_t length);
    int (*bulk_transfer)(usb_device_t* dev, uint8_t endpoint, void* data, uint32_t length);
    int (*interrupt_transfer)(usb_device_t* dev, uint8_t endpoint, void* data, uint32_t length);
    void (*poll)(struct usb_controller* ctrl);
} usb_controller_ops_t;

// USB Controller structure
typedef struct usb_controller {
    int type;               // UHCI, OHCI, EHCI, XHCI
    void* base;             // Memory-mapped or I/O base
    uint32_t io_base;       // I/O port base (for UHCI)
    uint8_t irq;
    usb_controller_ops_t* ops;
    void* driver_data;
    
    usb_device_t* devices[USB_MAX_DEVICES];
    int num_devices;
    int next_address;
} usb_controller_t;

// Controller types
#define USB_CONTROLLER_UHCI 0
#define USB_CONTROLLER_OHCI 1
#define USB_CONTROLLER_EHCI 2
#define USB_CONTROLLER_XHCI 3

// Initialize USB subsystem
void usb_init(void);

// Poll all USB controllers (call from main loop or timer)
void usb_poll(void);

// Register a USB device driver
typedef struct {
    const char* name;
    int (*probe)(usb_device_t* dev);
    void (*disconnect)(usb_device_t* dev);
} usb_driver_t;

void usb_register_driver(usb_driver_t* driver);

// USB device operations
int usb_control_transfer(usb_device_t* dev, uint8_t request_type, uint8_t request,
                         uint16_t value, uint16_t index, void* data, uint16_t length);
int usb_get_descriptor(usb_device_t* dev, uint8_t type, uint8_t index, void* data, uint16_t length);
int usb_set_configuration(usb_device_t* dev, uint8_t config);
int usb_set_interface(usb_device_t* dev, uint8_t interface, uint8_t alt_setting);

// USB keyboard interface (for keyboard.c to use)
int usb_keyboard_poll(void);
char usb_keyboard_getchar(void);
int usb_keyboard_has_key(void);

#endif // USB_H

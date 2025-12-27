#include "kernel/drivers/keyboard.h"

#include "kernel/cpu/ports.h"
#include "kernel/fs/device.h"
#include "kernel/kernel.h"
#include "libc/string.h"

static bool g_caps_lock = false;
static bool g_shift_pressed = false;
volatile char g_ch = 0, g_scan_code = 0;

static device_t g_keyboard_device;
static ssize_t keyboard_device_read(device_t* dev, uint32_t offset, uint32_t size, uint8_t* buffer);

void keyboard_handler(REGISTERS* r);

// see scan codes defined in keyboard.h for index
char g_scan_code_chars[128] = {0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=',
    '\b', '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0, 'a', 's', 'd',
    'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',',
    '.', '/', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '-', 0, 0, 0, '+', 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

void init_keyboard() {
    isr_register_handler(IRQ_BASE + 1, keyboard_handler);

    memset(&g_keyboard_device, 0, sizeof(g_keyboard_device));
    strncpy(g_keyboard_device.name, "kbd", DEVICE_NAME_MAX - 1);
    g_keyboard_device.type = DEVICE_TYPE_CHAR;
    g_keyboard_device.read = keyboard_device_read;
    device_register(&g_keyboard_device);
}

static int get_scancode() {
    int i, scancode = 0;

    // get scancode until status is on(key pressed)
    for (i = 1000; i > 0; i++) {
        // Check if scan code is ready
        if ((inportb(KEYBOARD_STATUS_PORT) & 1) == 0)
            continue;
        // Get the scan code
        scancode = inportb(KEYBOARD_DATA_PORT);
        break;
    }
    if (i > 0)
        return scancode;
    return 0;
}

char alternate_chars(char ch) {
    switch (ch) {
    case '`':
        return '~';
    case '1':
        return '!';
    case '2':
        return '@';
    case '3':
        return '#';
    case '4':
        return '$';
    case '5':
        return '%';
    case '6':
        return '^';
    case '7':
        return '&';
    case '8':
        return '*';
    case '9':
        return '(';
    case '0':
        return ')';
    case '-':
        return '_';
    case '=':
        return '+';
    case '[':
        return '{';
    case ']':
        return '}';
    case '\\':
        return '|';
    case ';':
        return ':';
    case '\'':
        return '\"';
    case ',':
        return '<';
    case '.':
        return '>';
    case '/':
        return '?';
    default:
        return ch;
    }
}

void keyboard_handler(REGISTERS* r) {
    int scancode;

    g_ch = 0;
    scancode = get_scancode();
    g_scan_code = scancode;
    if (scancode & 0x80) {
        // key release
    } else {
        // key down
        switch (scancode) {
        case SCAN_CODE_KEY_CAPS_LOCK:
            if (g_caps_lock == false)
                g_caps_lock = true;
            else
                g_caps_lock = false;
            break;

        case SCAN_CODE_KEY_ENTER:
            g_ch = '\n';
            break;

        case SCAN_CODE_KEY_TAB:
            g_ch = '\t';
            break;

        case SCAN_CODE_KEY_LEFT_SHIFT:
            g_shift_pressed = true;
            break;

        default:
            g_ch = g_scan_code_chars[scancode];
            // if caps in on, covert to upper
            if (g_caps_lock) {
                // if shift is pressed before
                if (g_shift_pressed) {
                    // replace alternate chars
                    g_ch = alternate_chars(g_ch);
                } else
                    g_ch = toupper(g_ch);
            } else {
                if (g_shift_pressed) {
                    if (isalpha(g_ch))
                        g_ch = toupper(g_ch);
                    else
                        // replace alternate chars
                        g_ch = alternate_chars(g_ch);
                } else
                    g_ch = g_scan_code_chars[scancode];
                g_shift_pressed = false;
            }
            break;
        }
    }
}

static ssize_t keyboard_device_read(device_t* dev, uint32_t offset, uint32_t size, uint8_t* buffer) {
    unused(dev);
    unused(offset);
    if (!buffer || size == 0) {
        return -1;
    }

    uint32_t read = 0;
    enable_interrupts();
    while (read < size) {
        buffer[read++] = (uint8_t) kb_getchar();
    }
    return (ssize_t) read;
}

// a blocking character read
char kb_getchar() {
    char c;

    while (g_ch <= 0)
        ;
    c = g_ch;
    g_ch = 0;
    g_scan_code = 0;
    return c;
}

char kb_get_scancode() {
    char code;

    while (g_scan_code <= 0)
        ;
    code = g_scan_code;
    g_ch = 0;
    g_scan_code = 0;
    return code;
}

// read string from console, and erase or go back util bound occurs
void getstr_bound(char* buffer, uint8_t bound) {
    if (!buffer)
        return;

    char* buf_start = buffer; // remember start
    while (1) {
        char ch = kb_getchar();

        if (ch == '\n') {
            kprintf("\n");
            *buffer = '\0';
            return;
        } else if (ch == '\b') {
            if (buffer > buf_start) { // only backspace if past start
                buffer--;
                *buffer = '\0';
                kprintf("\b"); // vbe_print_char erases
            }
        } else if (ch >= ' ' && ch <= '~') { // printable ASCII
            if ((buffer - buf_start) < bound) {
                *buffer++ = ch;
                kprintf("%c", ch);
            }
        }
        // ignore other control characters
    }
}

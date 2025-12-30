#include "kernel/drivers/keyboard.h"

#include "kernel/cpu/ports.h"
#include "kernel/fs/dev/dev_kbd.h"
#include "kernel/fs/device.h"
#include "kernel/kernel.h"
#include "libc/ctype.h"
#include "libc/string.h"

static bool g_caps_lock = false;
static bool g_shift_pressed = false;
static volatile char g_scan_code = 0;

// TTY ring buffer so multiple keystrokes are not lost and input is IRQ-safe.
#define TTYQ_LEN 128
static volatile uint32_t g_tty_r = 0;
static volatile uint32_t g_tty_w = 0;
static char g_tty_q[TTYQ_LEN];

static void ttyq_push(char c) {
    uint32_t irq = irq_save();
    uint32_t w = g_tty_w;
    uint32_t next = (w + 1) % TTYQ_LEN;
    if (next == g_tty_r) {
        // drop oldest
        g_tty_r = (g_tty_r + 1) % TTYQ_LEN;
    }
    g_tty_q[w] = c;
    g_tty_w = next;
    irq_restore(irq);
}

static int ttyq_pop(char* out) {
    if (!out) {
        return 0;
    }
    uint32_t irq = irq_save();
    if (g_tty_r == g_tty_w) {
        irq_restore(irq);
        return 0;
    }
    uint32_t r = g_tty_r;
    *out = g_tty_q[r];
    g_tty_r = (r + 1) % TTYQ_LEN;
    irq_restore(irq);
    return 1;
}

static device_t g_keyboard_device;
static ssize_t keyboard_device_read(device_t* dev, uint32_t offset, uint32_t size, uint8_t* buffer);

void keyboard_handler(REGISTERS* r);

// Scan code lookup table defined in keyboard.h order.
static const char g_scan_code_chars[128] = {0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0',
    '-', '=', '\b', '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0, 'a',
    's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\', 'z', 'x', 'c', 'v', 'b', 'n',
    'm', ',', '.', '/', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '-', 0, 0,
    0, '+', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

static inline bool is_shift_scancode(uint32_t code) {
    return code == SCAN_CODE_KEY_LEFT_SHIFT || code == SCAN_CODE_KEY_RIGHT_SHIFT;
}

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

static char resolve_printable_char(uint32_t scancode) {
    if (scancode >= sizeof(g_scan_code_chars)) {
        return 0;
    }

    char repr = g_scan_code_chars[scancode];
    if (!repr) {
        return 0;
    }

    if (isalpha(repr)) {
        bool upper = g_caps_lock ^ g_shift_pressed;
        return upper ? toupper(repr) : tolower(repr);
    }

    return g_shift_pressed ? alternate_chars(repr) : repr;
}

void keyboard_handler(REGISTERS* r) {
    unused(r);

    int scancode = get_scancode();
    if (!scancode) {
        return;
    }

    g_scan_code = scancode;

    if (scancode & 0x80) {
        uint32_t code = (uint32_t) (scancode & 0x7F);
        if (is_shift_scancode(code)) {
            g_shift_pressed = false;
        }
        dev_kbd_push_event(code, false, 0);
        return;
    }

    char repr = 0;
    switch (scancode) {
    case SCAN_CODE_KEY_CAPS_LOCK:
        g_caps_lock = !g_caps_lock;
        break;
    case SCAN_CODE_KEY_ENTER:
        repr = '\n';
        break;
    case SCAN_CODE_KEY_TAB:
        repr = '\t';
        break;
    case SCAN_CODE_KEY_BACKSPACE:
        repr = '\b';
        break;
    case SCAN_CODE_KEY_LEFT_SHIFT:
    case SCAN_CODE_KEY_RIGHT_SHIFT:
        g_shift_pressed = true;
        break;
    default:
        repr = resolve_printable_char((uint32_t) scancode);
        break;
    }

    if (repr) {
        ttyq_push(repr);
    }

    // Always push into /dev/kbd0 so userspace WM can read it.
    dev_kbd_push_event((uint32_t) scancode, true, repr);
}

static ssize_t keyboard_device_read(device_t* dev, uint32_t offset, uint32_t size, uint8_t* buffer) {
    unused(dev);
    unused(offset);
    if (!buffer || size == 0) {
        return -1;
    }

    // Non-blocking read.
    uint32_t read = 0;
    while (read < size) {
        char ch;
        if (!kb_try_getchar(&ch)) {
            break;
        }
        buffer[read++] = (uint8_t) ch;
    }
    return (ssize_t) read;
}

// a blocking character read
char kb_getchar() {
    char c = 0;
    while (!ttyq_pop(&c)) {
    }
    return c;
}

int kb_try_getchar(char* out) {
    if (!out) {
        return 0;
    }

    return ttyq_pop(out);
}

char kb_get_scancode() {
    char code;

    while (g_scan_code <= 0)
        ;
    code = g_scan_code;
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

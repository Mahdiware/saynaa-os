#include "kernel/drivers/keyboard.h"

#include "kernel/cpu/ports.h"
#include "kernel/fs/dev/dev_kbd.h"
#include "kernel/fs/device.h"
#include "kernel/kernel.h"
#include "libc/ctype.h"
#include "libc/string.h"

// PS/2 ports
#define KBD_DATA_PORT 0x60
#define KBD_STATUS_PORT 0x64

// TTY ring buffer
#define TTYQ_LEN 256

static char g_tty_q[TTYQ_LEN];
static uint32_t g_tty_r = 0;
static uint32_t g_tty_w = 0;
static bool g_tty_input_enabled = true;

// Keyboard state
static bool g_shift = false;
static bool g_caps = false;
static bool g_e0 = false;

// Device
static device_t g_kbd_dev;

// Scan code → ASCII (Set 1)
static const char g_scancode_map[128] = {0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0',
    '-', '=', '\b', '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0, 'a',
    's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\', 'z', 'x', 'c', 'v', 'b', 'n',
    'm', ',', '.', '/', 0, '*', 0, ' ', 0};

// Helpers
static inline void ttyq_push(char c) {
    uint32_t flags = irq_save();

    uint32_t next = (g_tty_w + 1) % TTYQ_LEN;
    if (next == g_tty_r) {
        g_tty_r = (g_tty_r + 1) % TTYQ_LEN;
    }

    g_tty_q[g_tty_w] = c;
    g_tty_w = next;

    irq_restore(flags);
}

static inline int ttyq_pop(char* out) {
    uint32_t flags = irq_save();

    if (g_tty_r == g_tty_w) {
        irq_restore(flags);
        return 0;
    }

    *out = g_tty_q[g_tty_r];
    g_tty_r = (g_tty_r + 1) % TTYQ_LEN;

    irq_restore(flags);
    return 1;
}

static inline char shift_char(char c) {
    switch (c) {
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
        return '"';
    case ',':
        return '<';
    case '.':
        return '>';
    case '/':
        return '?';
    case '`':
        return '~';
    default:
        return c;
    }
}

static inline char translate_key(uint8_t sc) {
    if (sc >= 128)
        return 0;

    char c = g_scancode_map[sc];
    if (!c)
        return 0;

    if (isalpha(c)) {
        bool upper = g_caps ^ g_shift;
        return upper ? toupper(c) : tolower(c);
    }

    return g_shift ? shift_char(c) : c;
}

// IRQ handler
void keyboard_handler(REGISTERS* r) {
    unused(r);

    uint8_t sc = inportb(KBD_DATA_PORT);

    /* Extended scancode prefix */
    if (sc == 0xE0) {
        g_e0 = true;
        return;
    }

    bool released = sc & 0x80;
    uint8_t code = sc & 0x7F;

    /* Ignore extended keys for now */
    if (g_e0) {
        g_e0 = false;
        return;
    }

    /* Modifier keys */
    if (code == SCAN_CODE_KEY_LEFT_SHIFT || code == SCAN_CODE_KEY_RIGHT_SHIFT) {
        g_shift = !released;
        return;
    }

    if (released)
        return;

    if (code == SCAN_CODE_KEY_CAPS_LOCK) {
        g_caps = !g_caps;
        return;
    }

    char ch = translate_key(code);
    if (ch) {
        if (g_tty_input_enabled) {
            ttyq_push(ch);
        }
        dev_kbd_push_event(code, true, ch);
    }
}

void keyboard_set_tty_input(bool enabled) {
    g_tty_input_enabled = enabled ? true : false;
}

// Device read
static ssize_t keyboard_read(device_t* dev, uint32_t off, uint32_t sz, uint8_t* buf) {
    unused(dev);
    unused(off);

    if (!buf || sz == 0)
        return -1;

    uint32_t n = 0;
    while (n < sz) {
        char c;
        if (!ttyq_pop(&c))
            break;
        buf[n++] = (uint8_t) c;
    }
    return (ssize_t) n;
}

// Public API
char kb_getchar(void) {
    char c;
    while (!ttyq_pop(&c))
        ;
    return c;
}

int kb_try_getchar(char* out) {
    if (!out)
        return 0;
    return ttyq_pop(out);
}

// Init
void init_keyboard(void) {
    isr_register_handler(IRQ_BASE + 1, keyboard_handler);

    memset(&g_kbd_dev, 0, sizeof(g_kbd_dev));
    strcpy(g_kbd_dev.name, "kbd");
    g_kbd_dev.type = DEVICE_TYPE_CHAR;
    g_kbd_dev.read = keyboard_read;

    device_register(&g_kbd_dev);

    kprintf("keyboard: ps/2 initialized\n");
}

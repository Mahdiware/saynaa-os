#include "kernel/fs/dev/dev_kbd.h"

#include "kernel/fs/device.h"
#include "kernel/kernel.h"
#include "libc/string.h"

#define KBDQ_LEN 128
static device_t g_dev_kbd0;

static kbd_event_t g_q[KBDQ_LEN];
static volatile uint32_t g_r = 0;
static volatile uint32_t g_w = 0;

void dev_kbd_push_event(uint32_t keycode, bool pressed, char repr) {
    kbd_event_t ev = {
        .keycode = keycode,
        .pressed = pressed ? 1 : 0,
        .repr = repr,
        ._pad = 0,
    };

    uint32_t w = g_w;
    uint32_t next = (w + 1) % KBDQ_LEN;
    if (next == g_r) {
        // drop oldest
        g_r = (g_r + 1) % KBDQ_LEN;
    }

    g_q[w] = ev;
    g_w = next;
}

static ssize_t dev_kbd_read(device_t* dev, uint32_t offset, uint32_t size, uint8_t* buffer) {
    unused(dev);
    unused(offset);

    if (!buffer || size < (uint32_t) sizeof(kbd_event_t)) {
        return -1;
    }

    uint32_t irq = irq_save();
    if (g_r == g_w) {
        irq_restore(irq);
        return 0;
    }

    uint32_t r = g_r;
    kbd_event_t ev = g_q[r];
    g_r = (r + 1) % KBDQ_LEN;
    irq_restore(irq);
    memcpy(buffer, &ev, sizeof(ev));
    return (ssize_t) sizeof(ev);
}

static ssize_t dev_kbd_write(device_t* dev, uint32_t offset, uint32_t size, const uint8_t* buffer) {
    unused(dev);
    unused(offset);
    unused(size);
    unused(buffer);
    return -1;
}

void init_dev_kbd(void) {
    memset(&g_dev_kbd0, 0, sizeof(g_dev_kbd0));
    strncpy(g_dev_kbd0.name, "kbd0", DEVICE_NAME_MAX - 1);
    g_dev_kbd0.type = DEVICE_TYPE_CHAR;
    g_dev_kbd0.read = dev_kbd_read;
    g_dev_kbd0.write = dev_kbd_write;

    g_r = 0;
    g_w = 0;

    device_register(&g_dev_kbd0);
}

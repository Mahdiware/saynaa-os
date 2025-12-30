#include "kernel/fs/dev/dev_mouse.h"

#include "kernel/fs/device.h"
#include "kernel/kernel.h"
#include "libc/string.h"

#define MOUSEQ_CAP 128

static device_t g_dev_mouse0;

static mouse_event_t g_q[MOUSEQ_CAP];
static volatile uint32_t g_r = 0;
static volatile uint32_t g_w = 0;

void dev_mouse_push_event(int32_t x, int32_t y, bool left, bool right, bool middle) {
    uint8_t buttons = 0;
    if (left)
        buttons |= 0x01;
    if (right)
        buttons |= 0x02;
    if (middle)
        buttons |= 0x04;

    uint32_t next = (g_w + 1) % MOUSEQ_CAP;
    if (next == g_r) {
        // drop oldest
        g_r = (g_r + 1) % MOUSEQ_CAP;
    }

    g_q[g_w] = (mouse_event_t) {.x = x, .y = y, .buttons = buttons, ._pad = {0, 0, 0}};
    g_w = next;
}

static ssize_t dev_mouse0_read(device_t* dev, uint32_t offset, uint32_t size, uint8_t* buffer) {
    unused(dev);
    unused(offset);

    if (!buffer || size < (uint32_t) sizeof(mouse_event_t)) {
        return -1;
    }

    if (g_r == g_w) {
        return 0; // no event
    }

    mouse_event_t ev = g_q[g_r];
    g_r = (g_r + 1) % MOUSEQ_CAP;

    memcpy(buffer, &ev, sizeof(ev));
    return (ssize_t) sizeof(ev);
}

static ssize_t dev_mouse0_write(device_t* dev, uint32_t offset, uint32_t size, const uint8_t* buffer) {
    unused(dev);
    unused(offset);
    unused(size);
    unused(buffer);
    return -1;
}

void init_dev_mouse(void) {
    memset(&g_dev_mouse0, 0, sizeof(g_dev_mouse0));
    strncpy(g_dev_mouse0.name, "mouse0", DEVICE_NAME_MAX - 1);
    g_dev_mouse0.type = DEVICE_TYPE_CHAR;
    g_dev_mouse0.read = dev_mouse0_read;
    g_dev_mouse0.write = dev_mouse0_write;

    g_r = 0;
    g_w = 0;

    device_register(&g_dev_mouse0);
}

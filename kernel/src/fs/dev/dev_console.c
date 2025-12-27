#include "kernel/fs/dev/dev_console.h"

#include "kernel/fs/dev/dev_tty.h"
#include "kernel/fs/device.h"
#include "kernel/kernel.h"
#include "libc/string.h"

static device_t g_dev_console;

#define CONSOLE_TTY_LINE 0

static ssize_t dev_console_read(device_t* dev, uint32_t offset, uint32_t size, uint8_t* buffer) {
    unused(dev);
    return ttydev_read_line(CONSOLE_TTY_LINE, offset, size, buffer);
}

static ssize_t dev_console_write(device_t* dev, uint32_t offset, uint32_t size, const uint8_t* buffer) {
    unused(dev);
    return ttydev_write_line(CONSOLE_TTY_LINE, offset, size, buffer);
}

void init_dev_console(void) {
    memset(&g_dev_console, 0, sizeof(g_dev_console));
    strncpy(g_dev_console.name, "console", DEVICE_NAME_MAX - 1);
    g_dev_console.type = DEVICE_TYPE_CHAR;
    g_dev_console.read = dev_console_read;
    g_dev_console.write = dev_console_write;
    device_register(&g_dev_console);
}

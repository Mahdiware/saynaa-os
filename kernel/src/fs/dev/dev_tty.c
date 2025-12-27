#include "kernel/fs/dev/dev_tty.h"

#include "kernel/drivers/keyboard.h"
#include "kernel/fs/dev/dev_tty.h"
#include "kernel/fs/device.h"
#include "kernel/kernel.h"
#include "libc/stdio.h"
#include "libc/string.h"

#include <stdbool.h>

typedef struct tty_binding {
    uint32_t line;
    bool use_active;
} tty_binding_t;

static device_t g_tty_devices[TTYDEV_VIRTUAL_CONSOLES];
static device_t g_tty_alias;
static tty_binding_t g_tty_bindings[TTYDEV_VIRTUAL_CONSOLES + 1];
static uint32_t g_active_line = 0;

static uint32_t tty_binding_line(const tty_binding_t* binding) {
    if (!binding) {
        return 0;
    }
    return binding->use_active ? g_active_line : binding->line;
}

static ssize_t tty_device_read(device_t* dev, uint32_t offset, uint32_t size, uint8_t* buffer) {
    tty_binding_t* binding = (tty_binding_t*) dev->private_data;
    uint32_t line = tty_binding_line(binding);
    return ttydev_read_line(line, offset, size, buffer);
}

static ssize_t tty_device_write(device_t* dev, uint32_t offset, uint32_t size, const uint8_t* buffer) {
    tty_binding_t* binding = (tty_binding_t*) dev->private_data;
    uint32_t line = tty_binding_line(binding);
    return ttydev_write_line(line, offset, size, buffer);
}

void ttydev_set_active_line(uint32_t line) {
    if (line >= TTYDEV_VIRTUAL_CONSOLES) {
        line = 0;
    }
    g_active_line = line;
}

uint32_t ttydev_active_line(void) {
    return g_active_line;
}

ssize_t ttydev_read_line(uint32_t line, uint32_t offset, uint32_t size, uint8_t* buffer) {
    unused(line);
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

ssize_t ttydev_write_line(uint32_t line, uint32_t offset, uint32_t size, const uint8_t* buffer) {
    unused(line);
    unused(offset);
    if (!buffer) {
        return -1;
    }
    for (uint32_t i = 0; i < size; i++) {
        vbe_print_char((char) buffer[i]);
    }
    return (ssize_t) size;
}

ssize_t ttydev_read_active(uint32_t offset, uint32_t size, uint8_t* buffer) {
    return ttydev_read_line(g_active_line, offset, size, buffer);
}

ssize_t ttydev_write_active(uint32_t offset, uint32_t size, const uint8_t* buffer) {
    return ttydev_write_line(g_active_line, offset, size, buffer);
}

void init_dev_tty(void) {
    for (uint32_t i = 0; i < TTYDEV_VIRTUAL_CONSOLES; i++) {
        memset(&g_tty_devices[i], 0, sizeof(device_t));
        snprintf(g_tty_devices[i].name, sizeof(g_tty_devices[i].name), "tty%u", i);
        g_tty_devices[i].type = DEVICE_TYPE_CHAR;
        g_tty_devices[i].read = tty_device_read;
        g_tty_devices[i].write = tty_device_write;
        g_tty_bindings[i] = (tty_binding_t) {.line = i, .use_active = false};
        g_tty_devices[i].private_data = &g_tty_bindings[i];
        device_register(&g_tty_devices[i]);
    }

    memset(&g_tty_alias, 0, sizeof(g_tty_alias));
    strncpy(g_tty_alias.name, "tty", DEVICE_NAME_MAX - 1);
    g_tty_alias.type = DEVICE_TYPE_CHAR;
    g_tty_alias.read = tty_device_read;
    g_tty_alias.write = tty_device_write;
    g_tty_bindings[TTYDEV_VIRTUAL_CONSOLES] = (tty_binding_t) {.line = 0, .use_active = true};
    g_tty_alias.private_data = &g_tty_bindings[TTYDEV_VIRTUAL_CONSOLES];
    device_register(&g_tty_alias);
}

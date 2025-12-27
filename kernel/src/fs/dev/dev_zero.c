#include "kernel/fs/dev/dev_zero.h"

#include "kernel/fs/device.h"
#include "kernel/kernel.h"
#include "libc/string.h"

static device_t g_dev_zero;

static ssize_t dev_zero_read(device_t* dev, uint32_t offset, uint32_t size, uint8_t* buffer) {
    unused(dev);
    unused(offset);
    if (!buffer) {
        return -1;
    }
    memset(buffer, 0, size);
    return (ssize_t) size;
}

static ssize_t dev_zero_write(device_t* dev, uint32_t offset, uint32_t size, const uint8_t* buffer) {
    unused(dev);
    unused(offset);
    unused(buffer);
    return (ssize_t) size;
}

void init_dev_zero(void) {
    memset(&g_dev_zero, 0, sizeof(g_dev_zero));
    strncpy(g_dev_zero.name, "zero", DEVICE_NAME_MAX - 1);
    g_dev_zero.type = DEVICE_TYPE_CHAR;
    g_dev_zero.read = dev_zero_read;
    g_dev_zero.write = dev_zero_write;
    device_register(&g_dev_zero);
}

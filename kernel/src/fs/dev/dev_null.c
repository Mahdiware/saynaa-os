#include "kernel/fs/dev/dev_null.h"

#include "kernel/fs/device.h"
#include "kernel/kernel.h"
#include "libc/string.h"

static device_t g_dev_null;

static ssize_t dev_null_read(device_t* dev, uint32_t offset, uint32_t size, uint8_t* buffer) {
    unused(dev);
    unused(offset);
    unused(buffer);
    return 0;
}

static ssize_t dev_null_write(device_t* dev, uint32_t offset, uint32_t size, const uint8_t* buffer) {
    unused(dev);
    unused(offset);
    unused(buffer);
    return (ssize_t) size;
}

void init_dev_null(void) {
    memset(&g_dev_null, 0, sizeof(g_dev_null));
    strncpy(g_dev_null.name, "null", DEVICE_NAME_MAX - 1);
    g_dev_null.type = DEVICE_TYPE_CHAR;
    g_dev_null.read = dev_null_read;
    g_dev_null.write = dev_null_write;
    device_register(&g_dev_null);
}

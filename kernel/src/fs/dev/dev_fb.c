#include "kernel/fs/dev/dev_fb.h"

#include "kernel/fs/device.h"
#include "kernel/kernel.h"
#include "kernel/lib/fb.h"
#include "libc/string.h"

static device_t g_dev_fb0;

static uint32_t fb0_size_bytes(void) {
    if (fb.pitch == 0 || fb.height == 0) {
        return 0;
    }
    return fb.pitch * fb.height;
}

static ssize_t dev_fb0_read(device_t* dev, uint32_t offset, uint32_t size, uint8_t* buffer) {
    unused(dev);

    if (!buffer || size == 0) {
        return -1;
    }

    fbdev_info_t info = {
        .width = fb.width,
        .height = fb.height,
        .pitch = fb.pitch,
        .bpp = fb.bpp,
        .size_bytes = fb0_size_bytes(),
    };

    const uint32_t header_size = (uint32_t) sizeof(info);
    const uint32_t fb_bytes = info.size_bytes;
    const uint32_t total_size = header_size + fb_bytes;

    if (offset >= total_size) {
        return 0;
    }

    uint32_t to_copy = size;
    if (to_copy > total_size - offset) {
        to_copy = total_size - offset;
    }

    uint32_t copied = 0;

    if (offset < header_size && copied < to_copy) {
        uint32_t hdr_off = offset;
        uint32_t hdr_can = header_size - hdr_off;
        uint32_t n = (to_copy - copied < hdr_can) ? (to_copy - copied) : hdr_can;
        memcpy(buffer + copied, ((uint8_t*) &info) + hdr_off, n);
        copied += n;
        offset += n;
    }

    if (offset >= header_size && copied < to_copy) {
        uint32_t fb_off = offset - header_size;
        if (fb_off >= fb_bytes) {
            return (ssize_t) copied;
        }
        uint32_t fb_can = fb_bytes - fb_off;
        uint32_t n = (to_copy - copied < fb_can) ? (to_copy - copied) : fb_can;
        memcpy(buffer + copied, (uint8_t*) fb.address + fb_off, n);
        copied += n;
    }

    return (ssize_t) copied;
}

static ssize_t dev_fb0_write(device_t* dev, uint32_t offset, uint32_t size, const uint8_t* buffer) {
    unused(dev);

    if (!buffer || size == 0) {
        return -1;
    }

    const uint32_t header_size = (uint32_t) sizeof(fbdev_info_t);
    const uint32_t fb_bytes = fb0_size_bytes();

    // Header is read-only
    if (offset < header_size) {
        return -1;
    }

    uint32_t fb_off = offset - header_size;
    if (fb_off >= fb_bytes) {
        return 0;
    }

    uint32_t to_copy = size;
    if (to_copy > fb_bytes - fb_off) {
        to_copy = fb_bytes - fb_off;
    }

    memcpy((uint8_t*) fb.address + fb_off, buffer, to_copy);
    return (ssize_t) to_copy;
}

void init_dev_fb(void) {
    memset(&g_dev_fb0, 0, sizeof(g_dev_fb0));
    strncpy(g_dev_fb0.name, "fb0", DEVICE_NAME_MAX - 1);
    g_dev_fb0.type = DEVICE_TYPE_CHAR;
    g_dev_fb0.read = dev_fb0_read;
    g_dev_fb0.write = dev_fb0_write;
    device_register(&g_dev_fb0);
}

#include "kernel/fs/device.h"

#include "kernel/kernel.h"
#include "kernel/lib/kprintf.h"
#include "libc/string.h"

#define DEVICE_MAX_COUNT 64

static device_t* g_devices[DEVICE_MAX_COUNT];
static uint32_t g_device_count = 0;

static ssize_t device_vfs_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    device_t* dev = node ? (device_t*) node->fs_private : NULL;
    if (!dev || !dev->read) {
        return -1;
    }
    return dev->read(dev, offset, size, buffer);
}

static ssize_t device_vfs_write(vfs_node_t* node, uint32_t offset, uint32_t size, const uint8_t* buffer) {
    device_t* dev = node ? (device_t*) node->fs_private : NULL;
    if (!dev || !dev->write) {
        return -1;
    }
    return dev->write(dev, offset, size, buffer);
}

static int device_vfs_ioctl(vfs_node_t* node, uint32_t request, void* arg) {
    device_t* dev = node ? (device_t*) node->fs_private : NULL;
    if (!dev || !dev->ioctl) {
        return -1;
    }
    return dev->ioctl(dev, request, arg);
}

bool device_register(device_t* dev) {
    if (!dev || dev->name[0] == '\0') {
        return false;
    }
    if (device_lookup(dev->name)) {
        kprintf("device: %s already registered\n", dev->name);
        return false;
    }
    if (g_device_count >= DEVICE_MAX_COUNT) {
        kprintf("device: registry full\n");
        return false;
    }

    memset(&dev->node, 0, sizeof(dev->node));
    strncpy(dev->node.name, dev->name, sizeof(dev->node.name) - 1);
    dev->node.flags = VFS_NODE_FILE;
    dev->node.fs_private = dev;
    dev->node.ops.read = dev->read ? device_vfs_read : NULL;
    dev->node.ops.write = dev->write ? device_vfs_write : NULL;
    dev->node.ops.ioctl = dev->ioctl ? device_vfs_ioctl : NULL;

    g_devices[g_device_count++] = dev;
    kprintf("device: registered %s\n", dev->name);
    return true;
}

uint32_t device_count(void) {
    return g_device_count;
}

device_t* device_by_index(uint32_t index) {
    if (index >= g_device_count) {
        return NULL;
    }
    return g_devices[index];
}

device_t* device_lookup(const char* name) {
    if (!name) {
        return NULL;
    }
    for (uint32_t i = 0; i < g_device_count; i++) {
        if (strcmp(name, g_devices[i]->name) == 0) {
            return g_devices[i];
        }
    }
    return NULL;
}

vfs_node_t* device_get_vfs_node(device_t* dev) {
    if (!dev) {
        return NULL;
    }
    return &dev->node;
}

#include "kernel/fs/devfs.h"

#include "kernel/drivers/keyboard.h"
#include "kernel/kernel.h"
#include "kernel/lib/kprintf.h"
#include "kernel/mem/malloc.h"
#include "libc/string.h"

#include <stdbool.h>

typedef struct devfs_entry {
    const char* name;
    vfs_node_t* node;
} devfs_entry_t;

static vfs_node_t g_root;
static vfs_node_t g_kbd;
static vfs_node_t g_null;
static bool g_inited = false;

static ssize_t dev_kbd_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer);
static ssize_t dev_null_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer);
static ssize_t dev_null_write(vfs_node_t* node, uint32_t offset, uint32_t size, const uint8_t* buffer);
static int dev_root_readdir(vfs_node_t* node, uint32_t index, vfs_dirent_t* dirent);
static vfs_node_t* dev_root_finddir(vfs_node_t* node, const char* name);

static devfs_entry_t g_entries[2];
static uint32_t g_entry_count = 0;

static void devfs_init_nodes(void) {
    if (g_inited) {
        return;
    }

    memset(&g_root, 0, sizeof(g_root));
    memset(&g_kbd, 0, sizeof(g_kbd));
    memset(&g_null, 0, sizeof(g_null));

    strncpy(g_root.name, "dev", sizeof(g_root.name) - 1);
    g_root.flags = VFS_NODE_DIR;
    g_root.ops.readdir = dev_root_readdir;
    g_root.ops.finddir = dev_root_finddir;

    strncpy(g_kbd.name, "kbd", sizeof(g_kbd.name) - 1);
    g_kbd.flags = VFS_NODE_FILE;
    g_kbd.ops.read = dev_kbd_read;

    strncpy(g_null.name, "null", sizeof(g_null.name) - 1);
    g_null.flags = VFS_NODE_FILE;
    g_null.ops.read = dev_null_read;
    g_null.ops.write = dev_null_write;

    g_entries[0] = (devfs_entry_t) {.name = "kbd", .node = &g_kbd};
    g_entries[1] = (devfs_entry_t) {.name = "null", .node = &g_null};
    g_entry_count = 2;

    g_inited = true;
}

static ssize_t dev_kbd_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    unused(node);
    unused(offset);
    if (!buffer || size == 0) {
        return -1;
    }

    uint32_t read = 0;
    while (read < size) {
        buffer[read++] = (uint8_t) kb_getchar();
    }
    return (ssize_t) read;
}

static ssize_t dev_null_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    unused(node);
    unused(offset);
    unused(buffer);
    return 0;
}

static ssize_t dev_null_write(vfs_node_t* node, uint32_t offset, uint32_t size, const uint8_t* buffer) {
    unused(node);
    unused(offset);
    unused(buffer);
    return (ssize_t) size;
}

static int dev_root_readdir(vfs_node_t* node, uint32_t index, vfs_dirent_t* dirent) {
    unused(node);
    if (!dirent) {
        return -1;
    }
    if (index >= g_entry_count) {
        return -1;
    }
    strncpy(dirent->name, g_entries[index].name, sizeof(dirent->name) - 1);
    dirent->name[sizeof(dirent->name) - 1] = '\0';
    dirent->inode = index + 1; // arbitrary non-zero inode
    return 0;
}

static vfs_node_t* dev_root_finddir(vfs_node_t* node, const char* name) {
    unused(node);
    if (!name) {
        return NULL;
    }
    for (uint32_t i = 0; i < g_entry_count; i++) {
        if (strcmp(name, g_entries[i].name) == 0) {
            return g_entries[i].node;
        }
    }
    return NULL;
}

vfs_node_t* devfs_get_root(void) {
    if (!g_inited) {
        devfs_init_nodes();
    }
    return &g_root;
}

static vfs_node_t* devfs_mount_internal(void* image, size_t size) {
    unused(image);
    unused(size);
    devfs_init_nodes();
    return &g_root;
}

const vfs_fs_ops_t devfs_driver = {.name = "devfs", .mount = devfs_mount_internal};

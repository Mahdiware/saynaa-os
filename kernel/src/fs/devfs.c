#include "kernel/fs/devfs.h"

#include "kernel/fs/device.h"
#include "kernel/kernel.h"
#include "libc/string.h"

#include <stdbool.h>

static vfs_node_t g_root;
static bool g_inited = false;

static int dev_root_readdir(vfs_node_t* node, uint32_t index, vfs_dirent_t* dirent);
static vfs_node_t* dev_root_finddir(vfs_node_t* node, const char* name);

static void devfs_init_nodes(void) {
    if (g_inited) {
        return;
    }

    memset(&g_root, 0, sizeof(g_root));

    strncpy(g_root.name, "dev", sizeof(g_root.name) - 1);
    g_root.flags = VFS_NODE_DIR;
    g_root.ops.readdir = dev_root_readdir;
    g_root.ops.finddir = dev_root_finddir;

    g_inited = true;
}

static int dev_root_readdir(vfs_node_t* node, uint32_t index, vfs_dirent_t* dirent) {
    unused(node);
    if (!dirent) {
        return -1;
    }
    device_t* dev = device_by_index(index);
    if (!dev) {
        return -1;
    }
    strncpy(dirent->name, dev->name, sizeof(dirent->name) - 1);
    dirent->name[sizeof(dirent->name) - 1] = '\0';
    dirent->inode = index + 1; // arbitrary non-zero inode
    return 0;
}

static vfs_node_t* dev_root_finddir(vfs_node_t* node, const char* name) {
    unused(node);
    device_t* dev = device_lookup(name);
    return device_get_vfs_node(dev);
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

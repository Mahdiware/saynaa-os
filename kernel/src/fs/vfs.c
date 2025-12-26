#include "kernel/fs/vfs.h"

#include "kernel/kernel.h"
#include "kernel/lib/kprintf.h"
#include "kernel/mem/malloc.h"
#include "libc/string.h"

#define VFS_MAX_FS 8

static const vfs_fs_ops_t* g_fs[VFS_MAX_FS];
static uint32_t g_fs_count = 0;
static vfs_node_t* g_root = NULL;

static vfs_node_t* vfs_walk(vfs_node_t* start, const char* path);

void vfs_init(void) {
    g_fs_count = 0;
    g_root = NULL;
    memset(g_fs, 0, sizeof(g_fs));
}

bool vfs_register_fs(const vfs_fs_ops_t* fs) {
    if (!fs || !fs->name || !fs->mount) {
        return false;
    }

    if (g_fs_count >= VFS_MAX_FS) {
        kprintf("vfs: registry full, cannot add %s\n", fs->name);
        return false;
    }

    g_fs[g_fs_count++] = fs;
    kprintf("vfs: registered fs driver '%s'\n", fs->name);
    return true;
}

vfs_node_t* vfs_mount(const char* fs_name, void* image, size_t size) {
    if (!fs_name) {
        return NULL;
    }

    for (uint32_t i = 0; i < g_fs_count; i++) {
        if (strcmp(fs_name, g_fs[i]->name) == 0) {
            return g_fs[i]->mount(image, size);
        }
    }

    kprintf("vfs: unknown fs '%s'\n", fs_name);
    return NULL;
}

bool vfs_set_root(vfs_node_t* root) {
    if (!root) {
        return false;
    }
    g_root = root;
    return true;
}

vfs_node_t* vfs_root(void) {
    return g_root;
}

vfs_node_t* vfs_lookup(const char* path) {
    if (!path || path[0] == '\0') {
        return g_root;
    }

    if (path[0] != '/') {
        // Only absolute paths are supported for now
        return NULL;
    }

    if (!g_root) {
        return NULL;
    }

    return vfs_walk(g_root, path);
}

ssize_t vfs_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    if (!node || !node->ops.read) {
        return -1;
    }
    return node->ops.read(node, offset, size, buffer);
}

ssize_t vfs_pread(const char* path, uint32_t offset, uint32_t size, uint8_t* buffer) {
    vfs_node_t* node = vfs_lookup(path);
    if (!node) {
        return -1;
    }
    return vfs_read(node, offset, size, buffer);
}

int vfs_readdir(vfs_node_t* node, uint32_t index, vfs_dirent_t* dirent) {
    if (!node || !node->ops.readdir) {
        return -1;
    }
    return node->ops.readdir(node, index, dirent);
}

static vfs_node_t* vfs_walk(vfs_node_t* start, const char* path) {
    const char* p = path;
    vfs_node_t* current = start;

    // Skip leading '/'
    while (*p == '/') {
        p++;
    }

    if (*p == '\0') {
        return current;
    }

    char segment[64];
    while (*p != '\0') {
        size_t len = 0;
        while (*p != '\0' && *p != '/' && len < sizeof(segment) - 1) {
            segment[len++] = *p++;
        }
        segment[len] = '\0';

        if (current->ops.finddir) {
            current = current->ops.finddir(current, segment);
        } else {
            current = NULL;
        }

        if (!current) {
            return NULL;
        }

        while (*p == '/') {
            p++;
        }
    }

    return current;
}

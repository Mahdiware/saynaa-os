#pragma once

#include "libc/stdint.h"
#include "libc/string.h"

// Signed size type for I/O operations
typedef int32_t ssize_t;

typedef struct vfs_node vfs_node_t;

typedef struct vfs_dirent {
    char name[256];
    uint32_t inode;
} vfs_dirent_t;

typedef struct vfs_file_ops {
    ssize_t (*read)(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer);
    ssize_t (*write)(vfs_node_t* node, uint32_t offset, uint32_t size, const uint8_t* buffer);
    int (*readdir)(vfs_node_t* node, uint32_t index, vfs_dirent_t* dirent);
    vfs_node_t* (*finddir)(vfs_node_t* node, const char* name);
} vfs_file_ops_t;

typedef struct vfs_node {
    char name[64];
    uint32_t flags;
    uint32_t length;
    void* fs_private;
    vfs_file_ops_t ops;
} vfs_node_t;

typedef struct vfs_fs_ops {
    const char* name;
    vfs_node_t* (*mount)(void* image, size_t size);
} vfs_fs_ops_t;

enum {
    VFS_NODE_FILE = 0x1,
    VFS_NODE_DIR = 0x2,
};

void vfs_init(void);
bool vfs_register_fs(const vfs_fs_ops_t* fs);
vfs_node_t* vfs_mount(const char* fs_name, void* image, size_t size);
bool vfs_set_root(vfs_node_t* root);
vfs_node_t* vfs_root(void);
vfs_node_t* vfs_lookup(const char* path);
ssize_t vfs_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer);
ssize_t vfs_pread(const char* path, uint32_t offset, uint32_t size, uint8_t* buffer);
int vfs_readdir(vfs_node_t* node, uint32_t index, vfs_dirent_t* dirent);

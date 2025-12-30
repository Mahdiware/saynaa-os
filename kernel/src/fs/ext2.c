#include "kernel/fs/ext2.h"

#include "kernel/fs/devfs.h"
#include "kernel/kernel.h"
#include "kernel/mem/malloc.h"
#include "kernel/utils/debug.h"
#include "libc/string.h"

typedef struct ext2_fs {
    uint8_t* image;
    size_t size;
    ext2_superblock_t* sb;
    uint32_t block_size;
    uint16_t inode_size;
} ext2_fs_t;

typedef struct ext2_handle {
    ext2_fs_t* fs;
    uint32_t inode;
} ext2_handle_t;

// File type flags in inode->mode
#define EXT2_S_IFDIR 0x4000
#define EXT2_S_IFREG 0x8000

static ssize_t ext2_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer);
static ssize_t ext2_write(vfs_node_t* node, uint32_t offset, uint32_t size, const uint8_t* buffer);
static int ext2_readdir(vfs_node_t* node, uint32_t index, vfs_dirent_t* dirent);
static vfs_node_t* ext2_finddir(vfs_node_t* node, const char* name);

static void populate_root_node(vfs_node_t* node, ext2_fs_t* fs) {
    memset(node, 0, sizeof(vfs_node_t));
    node->flags = VFS_NODE_DIR;
    node->length = 0;
    ext2_handle_t* h = kmalloc(sizeof(ext2_handle_t));
    h->fs = fs;
    h->inode = 2; // root inode
    node->fs_private = h;
    node->ops.read = ext2_read;
    node->ops.write = ext2_write;
    node->ops.readdir = ext2_readdir;
    node->ops.finddir = ext2_finddir;
    node->name[0] = '/';
    node->name[1] = '\0';
}

static inline void* ext2_block_ptr(ext2_fs_t* fs, uint32_t block) {
    uint32_t offset = block * fs->block_size;
    if (offset >= fs->size) {
        return NULL;
    }
    return fs->image + offset;
}

static ext2_block_group_desc_t* ext2_bg_desc(ext2_fs_t* fs, uint32_t group) {
    uint32_t bgdt_offset = (fs->block_size == 1024) ? fs->block_size * 2 : fs->block_size;
    uint32_t offset = bgdt_offset + group * sizeof(ext2_block_group_desc_t);
    if (offset + sizeof(ext2_block_group_desc_t) > fs->size) {
        return NULL;
    }
    return (ext2_block_group_desc_t*) (fs->image + offset);
}

static ext2_inode_t* ext2_get_inode(ext2_fs_t* fs, uint32_t inode) {
    if (inode == 0) {
        return NULL;
    }
    uint32_t group = (inode - 1) / fs->sb->inodes_per_group;
    uint32_t index = (inode - 1) % fs->sb->inodes_per_group;

    ext2_block_group_desc_t* bgd = ext2_bg_desc(fs, group);
    if (!bgd) {
        return NULL;
    }

    uint32_t table_block = bgd->inode_table;
    uint32_t offset = table_block * fs->block_size + index * fs->inode_size;
    if (offset + fs->inode_size > fs->size) {
        return NULL;
    }

    return (ext2_inode_t*) (fs->image + offset);
}

static uint32_t ext2_inode_mode_type(ext2_inode_t* inode) {
    return inode->mode & 0xF000;
}

static uint32_t ext2_get_data_block(ext2_fs_t* fs, ext2_inode_t* inode, uint32_t block_index) {
    if (block_index < 12) {
        return inode->block[block_index];
    }

    uint32_t idx = block_index - 12;
    uint32_t per_block = fs->block_size / sizeof(uint32_t);

    if (idx < per_block && inode->block[12]) {
        uint32_t* indir = (uint32_t*) ext2_block_ptr(fs, inode->block[12]);
        if (indir) {
            return indir[idx];
        }
    }

    return 0;
}

static ssize_t ext2_read_file(ext2_fs_t* fs, ext2_inode_t* inode, uint32_t offset, uint32_t size, uint8_t* buffer) {
    if (!inode) {
        return -1;
    }
    if (offset >= inode->size) {
        return 0;
    }
    if (offset + size > inode->size) {
        size = inode->size - offset;
    }

    uint32_t block_size = fs->block_size;
    uint32_t block_index = offset / block_size;
    uint32_t block_offset = offset % block_size;
    uint32_t copied = 0;

    while (copied < size) {
        uint32_t blk = ext2_get_data_block(fs, inode, block_index);
        if (!blk) {
            uint32_t chunk = block_size - block_offset;
            if (chunk > size - copied) {
                chunk = size - copied;
            }
            memset(buffer + copied, 0, chunk);
            copied += chunk;
            block_index++;
            block_offset = 0;
            continue;
        }
        uint8_t* blk_ptr = ext2_block_ptr(fs, blk);
        if (!blk_ptr) {
            kprintf_error("ext2: bad block ptr %u", blk);
            break;
        }

        uint32_t chunk = block_size - block_offset;
        if (chunk > size - copied) {
            chunk = size - copied;
        }

        memcpy(buffer + copied, blk_ptr + block_offset, chunk);
        copied += chunk;
        block_index++;
        block_offset = 0;
    }

    return copied;
}

static vfs_node_t* ext2_make_node(ext2_fs_t* fs, uint32_t inode_no, const char* name) {
    ext2_inode_t* inode = ext2_get_inode(fs, inode_no);
    if (!inode) {
        return NULL;
    }

    vfs_node_t* node = kmalloc(sizeof(vfs_node_t));
    if (!node) {
        return NULL;
    }

    memset(node, 0, sizeof(vfs_node_t));
    node->length = inode->size;
    uint32_t type = ext2_inode_mode_type(inode);
    node->flags = (type == EXT2_S_IFDIR) ? VFS_NODE_DIR : VFS_NODE_FILE;
    strncpy(node->name, name, sizeof(node->name) - 1);

    ext2_handle_t* h = kmalloc(sizeof(ext2_handle_t));
    if (!h) {
        kfree(node);
        return NULL;
    }
    h->fs = fs;
    h->inode = inode_no;
    node->fs_private = h;
    node->ops.read = ext2_read;
    node->ops.write = ext2_write;
    node->ops.readdir = ext2_readdir;
    node->ops.finddir = ext2_finddir;
    return node;
}

static ext2_fs_t* ext2_create_fs(void* image, size_t size) {
    if (size < 2048) {
        return NULL;
    }

    uint8_t* base = (uint8_t*) image;
    ext2_superblock_t* sb = (ext2_superblock_t*) (base + 1024);

    if (sb->magic != EXT2_SUPER_MAGIC) {
        return NULL;
    }

    ext2_fs_t* fs = (ext2_fs_t*) kmalloc(sizeof(ext2_fs_t));
    if (!fs) {
        return NULL;
    }

    fs->image = base;
    fs->size = size;
    fs->sb = sb;
    fs->block_size = 1024u << sb->log_block_size;
    fs->inode_size = sb->inode_size ? sb->inode_size : 128;
    return fs;
}

vfs_node_t* ext2_mount(void* image, size_t size) {
    ext2_fs_t* fs = ext2_create_fs(image, size);
    if (!fs) {
        return NULL;
    }

    vfs_node_t* root = (vfs_node_t*) kmalloc(sizeof(vfs_node_t));
    if (!root) {
        kfree(fs);
        return NULL;
    }

    populate_root_node(root, fs);

    ext2_inode_t* root_ino = ext2_get_inode(fs, 2);
    if (root_ino) {
        root->length = root_ino->size;
    }

    kprintf("ext2: mounted ramdisk image (block size %u, inodes %u, blocks %u)\n", fs->block_size,
        fs->sb->inodes_count, fs->sb->blocks_count);

    return root;
}

static ssize_t ext2_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    if (!node || !node->fs_private) {
        return -1;
    }
    ext2_handle_t* h = (ext2_handle_t*) node->fs_private;
    ext2_inode_t* inode = ext2_get_inode(h->fs, h->inode);
    if (!inode || (inode->mode & EXT2_S_IFDIR)) {
        return -1;
    }
    return ext2_read_file(h->fs, inode, offset, size, buffer);
}

static ssize_t ext2_write(vfs_node_t* node, uint32_t offset, uint32_t size, const uint8_t* buffer) {
    unused(node);
    unused(offset);
    unused(size);
    unused(buffer);
    // Read-only stub for now
    return -1;
}

static int ext2_readdir(vfs_node_t* node, uint32_t index, vfs_dirent_t* dirent) {
    if (!node || !node->fs_private || !dirent) {
        return -1;
    }

    ext2_handle_t* h = (ext2_handle_t*) node->fs_private;
    ext2_inode_t* inode = ext2_get_inode(h->fs, h->inode);
    if (!inode || !(inode->mode & EXT2_S_IFDIR)) {
        return -1;
    }

    bool is_root = (h->inode == 2);
    uint8_t* blk_ptr;
    uint32_t block_size = h->fs->block_size;
    uint32_t seen = 0;

    for (uint32_t b = 0; b < 12; b++) { // direct blocks
        uint32_t blk = inode->block[b];
        if (!blk) {
            continue;
        }
        blk_ptr = ext2_block_ptr(h->fs, blk);
        if (!blk_ptr) {
            continue;
        }

        uint32_t offset = 0;
        while (offset < block_size) {
            ext2_dirent_t* d = (ext2_dirent_t*) (blk_ptr + offset);
            if (d->inode && d->name_len && d->rec_len) {
                if (seen == index) {
                    uint32_t name_len = d->name_len < sizeof(dirent->name) - 1 ? d->name_len
                                                                               : sizeof(dirent->name) - 1;
                    memcpy(dirent->name, d->name, name_len);
                    dirent->name[name_len] = '\0';
                    dirent->inode = d->inode;
                    return 0;
                }
                seen++;
            }
            if (!d->rec_len) {
                break;
            }
            offset += d->rec_len;
        }
    }

    if (is_root && index == seen) {
        strncpy(dirent->name, "dev", sizeof(dirent->name) - 1);
        dirent->name[sizeof(dirent->name) - 1] = '\0';
        dirent->inode = 0;
        return 0;
    }

    return -1;
}

static vfs_node_t* ext2_finddir(vfs_node_t* node, const char* name) {
    if (!node || !node->fs_private || !name) {
        return NULL;
    }

    ext2_handle_t* h = (ext2_handle_t*) node->fs_private;
    ext2_inode_t* inode = ext2_get_inode(h->fs, h->inode);
    if (!inode || !(inode->mode & EXT2_S_IFDIR)) {
        return NULL;
    }

    if (h->inode == 2 && strcmp(name, "dev") == 0) {
        return devfs_get_root();
    }

    uint32_t block_size = h->fs->block_size;

    for (uint32_t b = 0; b < 12; b++) { // direct blocks only
        uint32_t blk = inode->block[b];
        if (!blk) {
            continue;
        }
        uint8_t* blk_ptr = ext2_block_ptr(h->fs, blk);
        if (!blk_ptr) {
            continue;
        }

        uint32_t offset = 0;
        while (offset < block_size) {
            ext2_dirent_t* d = (ext2_dirent_t*) (blk_ptr + offset);
            if (d->inode && d->name_len && d->rec_len) {
                if (d->name_len == strlen(name) && strncmp(d->name, name, d->name_len) == 0) {
                    return ext2_make_node(h->fs, d->inode, name);
                }
            }
            if (!d->rec_len) {
                break;
            }
            offset += d->rec_len;
        }
    }

    return NULL;
}

const vfs_fs_ops_t ext2_driver = {
    .name = "ext2",
    .mount = ext2_mount,
};

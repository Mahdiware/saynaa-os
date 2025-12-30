#pragma once

#include "kernel/fs/vfs.h"

#define DEVICE_NAME_MAX 32

typedef enum {
    DEVICE_TYPE_CHAR = 1,
    DEVICE_TYPE_BLOCK = 2,
} device_type_t;

struct device;

typedef ssize_t (*device_read_block_t)(struct device* dev, uint32_t block, uint32_t count, uint8_t* buffer);
typedef ssize_t (*device_write_block_t)(struct device* dev, uint32_t block, uint32_t count, const uint8_t* buffer);
typedef ssize_t (*device_read_t)(struct device* dev, uint32_t offset, uint32_t size, uint8_t* buffer);
typedef ssize_t (*device_write_t)(struct device* dev, uint32_t offset, uint32_t size, const uint8_t* buffer);
typedef bool (*device_ready_t)(struct device* dev);
typedef int (*device_open_t)(struct device* dev);
typedef int (*device_close_t)(struct device* dev);
typedef int (*device_ioctl_t)(struct device* dev, uint32_t request, void* arg);
typedef int (*device_ftruncate_t)(struct device* dev, uint32_t length);
typedef void* (*device_mmap_t)(struct device* dev, void* addr);
typedef int (*device_munmap_t)(struct device* dev, void* addr);

typedef struct device {
    char name[DEVICE_NAME_MAX];
    device_type_t type;
    device_read_block_t read_block;
    device_write_block_t write_block;
    device_read_t read;
    device_write_t write;
    device_ready_t read_ready;
    device_ready_t write_ready;
    device_open_t open;
    device_close_t close;
    device_ioctl_t ioctl;
    device_ftruncate_t ftruncate;
    device_mmap_t mmap;
    device_munmap_t munmap;
    void* private_data;
    vfs_node_t node;
} device_t;

bool device_register(device_t* dev);
uint32_t device_count(void);
device_t* device_by_index(uint32_t index);
device_t* device_lookup(const char* name);
vfs_node_t* device_get_vfs_node(device_t* dev);

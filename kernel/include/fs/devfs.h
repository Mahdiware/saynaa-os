#pragma once

#include "kernel/fs/vfs.h"

// Simple in-memory device filesystem exposing a few character devices.

extern const vfs_fs_ops_t devfs_driver;

// Returns the root node of devfs (lazy-initialized).
vfs_node_t* devfs_get_root(void);

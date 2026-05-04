#pragma once

#include "kernel/fs/vfs.h"
#include "libc/stdint.h"

void init_dev_pty(void);

// Opens a new PTY master node and returns its VFS node.
vfs_node_t* pty_open_master(uint32_t* out_id);

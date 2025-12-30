#pragma once

#include "libc/stdint.h"

#define SYSCALL_NUM 256

#define SYS_EXIT 1
#define SYS_PUTCHAR 2
#define SYS_WRITE 3
#define SYS_GETPID 4
#define SYS_YIELD 5
#define SYS_READ 6
#define SYS_READDIR 7
#define SYS_EXEC 8
#define SYS_STAT 9
#define SYS_GETCHAR 10
#define SYS_WAITPID 11
#define SYS_GETCWD 12
#define SYS_CHDIR 13
#define SYS_SLEEP 15
#define SYS_SBRK 16
#define SYS_MAKETTY 18
#define SYS_READSTDOUT 19
#define SYS_SHM_CREATE 20
#define SYS_SHM_MAP 21
#define SYS_SHM_CLOSE 22
#define SYS_OPEN 23
#define SYS_CLOSE 24
#define SYS_SEEK 25

// Shared flags and structs
#define O_RDONLY (1u << 0)
#define O_WRONLY (1u << 1)
#define O_RDWR (O_RDONLY | O_WRONLY)
#define O_APPEND (1u << 2)
#define O_TRUNC (1u << 3)

#define SYS_SEEK_SET 0
#define SYS_SEEK_CUR 1
#define SYS_SEEK_END 2

typedef struct sys_dirent {
    char name[256];
    uint32_t inode;
} sys_dirent_t;

typedef struct sys_stat {
    uint32_t flags;
    uint32_t size;
} sys_stat_t;

#define SYS_NODE_FILE 0x1
#define SYS_NODE_DIR 0x2
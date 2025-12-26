#pragma once

#include "libc/stdint.h"

enum {
    SYS_exit = 1,
    SYS_putchar = 2,
    SYS_write = 3,
    SYS_getpid = 4,
    SYS_yield = 5,
    SYS_readfile = 6,
    SYS_readdir = 7,
    SYS_exec = 8,
    SYS_stat = 9,
    SYS_getchar = 10,
    SYS_waitpid = 11,
    SYS_getcwd = 12,
    SYS_chdir = 13,
};

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

static inline int sys_putchar(char c) {
    int ret;
    asm volatile("int $0x30" : "=a"(ret) : "a"(SYS_putchar), "b"(c) : "memory");
    return ret;
}

static inline int sys_write(const char* buf, uint32_t len) {
    int ret;
    asm volatile("int $0x30" : "=a"(ret) : "a"(SYS_write), "b"(buf), "c"(len) : "memory");
    return ret;
}

static inline int sys_exit(void) {
    asm volatile("int $0x30" : : "a"(SYS_exit) : "memory");
    return 0;
}

static inline int sys_getpid(void) {
    int ret;
    asm volatile("int $0x30" : "=a"(ret) : "a"(SYS_getpid) : "memory");
    return ret;
}

static inline int sys_yield(void) {
    int ret;
    asm volatile("int $0x30" : "=a"(ret) : "a"(SYS_yield) : "memory");
    return ret;
}

static inline int sys_readfile(const char* path, uint32_t offset, uint32_t size, void* buf) {
    int ret;
    asm volatile("int $0x30"
        : "=a"(ret)
        : "a"(SYS_readfile), "b"(path), "c"(offset), "d"(size), "S"(buf)
        : "memory");
    return ret;
}

static inline int sys_readdir(const char* path, uint32_t index, sys_dirent_t* dirent) {
    int ret;
    asm volatile("int $0x30"
        : "=a"(ret)
        : "a"(SYS_readdir), "b"(path), "c"(index), "d"(dirent)
        : "memory");
    return ret;
}

static inline int sys_exec(const char* path, char* const argv[]) {
    int ret;
    asm volatile("int $0x30" : "=a"(ret) : "a"(SYS_exec), "b"(path), "c"(argv) : "memory");
    return ret;
}

static inline int sys_stat(const char* path, sys_stat_t* st) {
    int ret;
    asm volatile("int $0x30" : "=a"(ret) : "a"(SYS_stat), "b"(path), "c"(st) : "memory");
    return ret;
}

static inline int sys_getchar(void) {
    int ret;
    asm volatile("int $0x30" : "=a"(ret) : "a"(SYS_getchar) : "memory");
    return ret;
}

static inline int sys_waitpid(int pid) {
    int ret;
    asm volatile("int $0x30" : "=a"(ret) : "a"(SYS_waitpid), "b"(pid) : "memory");
    return ret;
}

static inline int sys_getcwd(char* buf, uint32_t len) {
    int ret;
    asm volatile("int $0x30" : "=a"(ret) : "a"(SYS_getcwd), "b"(buf), "c"(len) : "memory");
    return ret;
}

static inline int sys_chdir(const char* path) {
    int ret;
    asm volatile("int $0x30" : "=a"(ret) : "a"(SYS_chdir), "b"(path) : "memory");
    return ret;
}

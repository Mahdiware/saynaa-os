#pragma once

#include "kernel/api/syscall_api.h"
#include "libc/stdint.h"

uint32_t syscall0(uint32_t num);
uint32_t syscall1(uint32_t num, uint32_t arg1);
uint32_t syscall2(uint32_t num, uint32_t arg1, uint32_t arg2);
uint32_t syscall3(uint32_t num, uint32_t arg1, uint32_t arg2, uint32_t arg3);

static inline int sys_open(const char* path, uint32_t flags) {
    return (int) syscall2(SYS_OPEN, (uint32_t) path, flags);
}

static inline int sys_close(int fd) {
    return (int) syscall1(SYS_CLOSE, (uint32_t) fd);
}

static inline int sys_read(int fd, void* buf, uint32_t size) {
    return (int) syscall3(SYS_READ, (uint32_t) fd, (uint32_t) buf, size);
}

static inline int sys_write(int fd, const void* buf, uint32_t size) {
    return (int) syscall3(SYS_WRITE, (uint32_t) fd, (uint32_t) buf, size);
}

static inline int sys_seek(int fd, int32_t offset, int whence) {
    return (int) syscall3(SYS_SEEK, (uint32_t) fd, (uint32_t) offset, (uint32_t) whence);
}

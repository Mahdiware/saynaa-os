#include "libc/syscall.h"

uint32_t syscall0(uint32_t num) {
    uint32_t ret;
    __asm__ volatile("int $0x30" : "=a"(ret) : "a"(num) : "memory");
    return ret;
}

uint32_t syscall1(uint32_t num, uint32_t arg1) {
    uint32_t ret;
    __asm__ volatile("int $0x30" : "=a"(ret) : "a"(num), "b"(arg1) : "memory");
    return ret;
}

uint32_t syscall2(uint32_t num, uint32_t arg1, uint32_t arg2) {
    uint32_t ret;
    __asm__ volatile("int $0x30" : "=a"(ret) : "a"(num), "b"(arg1), "c"(arg2) : "memory");
    return ret;
}

uint32_t syscall3(uint32_t num, uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    uint32_t ret;
    __asm__ volatile("int $0x30"
        : "=a"(ret)
        : "a"(num), "b"(arg1), "c"(arg2), "d"(arg3)
        : "memory");
    return ret;
}
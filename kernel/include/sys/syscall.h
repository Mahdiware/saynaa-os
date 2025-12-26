#pragma once

#include "kernel/cpu/isr.h"
#include "libc/stdint.h"

#define SYSCALL_NUM 256

enum syscall_no {
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

typedef void (*sys_handler_t)(REGISTERS*);

void init_syscall();

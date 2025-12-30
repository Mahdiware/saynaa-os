#pragma once

#include "kernel/api/syscall_api.h"
#include "kernel/cpu/isr.h"
#include "libc/stdint.h"

typedef void (*sys_handler_t)(REGISTERS*);

void init_syscall();

#pragma once

#include "kernel/lib/kprintf.h"
#include "libc/stdint.h"
#include "libc/string.h"

#define STR(x) #x
#define assert(expr) \
    do { \
        if (!(expr)) { \
            printf("Assertion failed: %s, file %s, line %d\n", STR(expr), __FILE__, __LINE__); \
            while (1) { \
            } \
        } \
    } while (0)

#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : (char*) __FILE__)

#define kprintf_info(format, ...) \
    kserialf("[\x1B[32m%s\x1B[0m] " format "\n", __FILENAME__, ##__VA_ARGS__)

#define kprintf_error(format, ...) \
    kserialf("[\x1B[31;1m%s\x1B[0m] " format "\n", __FILENAME__, ##__VA_ARGS__)

#define kprintf_warn(format, ...) \
    kserialf("[\x1B[33m%s\x1B[0m] " format "\n", __FILENAME__, ##__VA_ARGS__)

#define abort() \
    kprintf_error("Kernel Panic: abort()\n"); \
    while (1) { \
    };
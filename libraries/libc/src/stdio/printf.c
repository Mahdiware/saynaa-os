#include "libc/stdio.h"
#include "libc/string.h"
#include "libc/syscall.h"

#include <stdarg.h>

int printf(const char* restrict format, ...) {
    char buf[512];
    va_list args;
    va_start(args, format);
    int n = vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    if (n < 0) {
        return n;
    }
    uint32_t len = (uint32_t) ((n < (int) sizeof(buf)) ? n : (int) sizeof(buf));
    sys_write(buf, len);
    return n;
}

int puts(const char* s) {
    if (!s) {
        return -1;
    }
    uint32_t len = strlen(s);
    sys_write(s, len);
    sys_write("\n", 1);
    return (int) len + 1;
}

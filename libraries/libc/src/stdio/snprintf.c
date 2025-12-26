#include "libc/stdio.h"

#include <stdarg.h>

int snprintf(char* restrict str, size_t size, const char* restrict format, ...) {
    va_list args;
    va_start(args, format);
    int r = vsnprintf(str, size, format, args);
    va_end(args);
    return r;
}

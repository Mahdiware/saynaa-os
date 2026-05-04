#pragma once

#include "libc/stdint.h"

#include <stdarg.h>

int vsprintf(char* restrict str, const char* restrict format, va_list list);
int vsnprintf(char* restrict str, size_t size, const char* restrict format, va_list list);
int vcbprintf(void* ctx, size_t (*callback)(void*, const char*, size_t), const char* format, va_list parameters);
int snprintf(char* restrict str, size_t size, const char* restrict format, ...);
int printf(const char* restrict format, ...);
int puts(const char* s);
int snprintf(char* restrict str, size_t size, const char* restrict format, ...);
int scanf(const char* restrict format, ...);
int vscanf(const char* restrict format, va_list list);
int sscanf(const char* restrict str, const char* restrict format, ...);
int vsscanf(const char* restrict str, const char* restrict format, va_list list);
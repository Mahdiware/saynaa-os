#pragma once

#include "libc/stdint.h"

int make_abs_path(const char* cwd, const char* input, char* out, size_t out_len);
int normalize_path(char* path);

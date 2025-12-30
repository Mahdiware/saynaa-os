#pragma once

#include "libc/stdint.h"

void init_dev_fb(void);

typedef struct fbdev_info {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint32_t size_bytes;
} fbdev_info_t;

#pragma once

#include "stdint.h"

// Must match kernel/include/fs/dev/dev_fb.h
typedef struct fbdev_info {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint32_t size_bytes;
} fbdev_info_t;

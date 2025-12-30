#pragma once

#include "stdint.h"

// Must match kernel/include/fs/dev/dev_mouse.h
typedef struct mouse_event {
    int32_t x;
    int32_t y;
    uint8_t buttons; // bit0=left, bit1=right, bit2=middle
    uint8_t _pad[3];
} mouse_event_t;

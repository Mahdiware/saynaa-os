#pragma once

#include "libc/stdint.h"

void init_dev_mouse(void);

// Userspace-visible mouse event structure.
typedef struct mouse_event {
    int32_t x;
    int32_t y;
    uint8_t buttons; // bit0=left, bit1=right, bit2=middle
    uint8_t _pad[3];
} mouse_event_t;

// Called from the mouse driver (IRQ context) to enqueue events.
void dev_mouse_push_event(int32_t x, int32_t y, bool left, bool right, bool middle);

#pragma once

#include "libc/stdint.h"

// Userspace-visible keyboard event.
// This is a *separate* stream from tty input, intended for the userspace WM.

typedef struct kbd_event {
    uint32_t keycode; // scancode (set 1)
    uint8_t pressed;  // 1=down, 0=up
    char repr;        // best-effort ASCII representation (0 if none)
    uint8_t _pad;
} kbd_event_t;

void init_dev_kbd(void);
void dev_kbd_push_event(uint32_t keycode, bool pressed, char repr);

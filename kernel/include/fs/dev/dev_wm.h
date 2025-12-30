#pragma once

#include "libc/stdint.h"

void init_dev_wm(void);

// Client->/dev/wm write header
// dst=0 means "send to wm server".
typedef struct wm_send_hdr {
    uint32_t dst;
    uint32_t type;
    uint32_t len;
} wm_send_hdr_t;

// /dev/wm read header
// src is filled by the kernel.
typedef struct wm_recv_hdr {
    uint32_t src;
    uint32_t type;
    uint32_t len;
} wm_recv_hdr_t;

#define WM_MAX_PAYLOAD 256

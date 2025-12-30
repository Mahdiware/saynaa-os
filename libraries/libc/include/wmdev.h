#pragma once

#include "stdint.h"

#define WM_MAX_PAYLOAD 256

// Client -> /dev/wm write header.
// dst=0 means "send to wm server".
typedef struct wm_send_hdr {
    uint32_t dst;
    uint32_t type;
    uint32_t len;
} wm_send_hdr_t;

// /dev/wm read header.
// src is filled by the kernel.
typedef struct wm_recv_hdr {
    uint32_t src;
    uint32_t type;
    uint32_t len;
} wm_recv_hdr_t;

// ---- IPC protocol (userspace WM) ----
// All payload structs are fixed-size and must fit within WM_MAX_PAYLOAD.

enum {
    WM_MSG_NOP = 0,
    WM_MSG_CREATE = 1,
    WM_MSG_CREATE_REPLY = 2,
    WM_MSG_CLOSE = 3,
    WM_MSG_DAMAGE = 4,
    WM_MSG_EVENT = 5,
};

// Mouse event semantics mirror the legacy kernel WM:
// - x,y are relative to window top-left
// - a 16x16 cursor box is implied by clients
enum {
    WM_IPC_EVENT_MOUSE_PRESS = 1,
    WM_IPC_EVENT_MOUSE_RELEASE = 2,
    WM_IPC_EVENT_MOUSE_MOVE = 3,
    WM_IPC_EVENT_MOUSE_ENTER = 4,
    WM_IPC_EVENT_MOUSE_EXIT = 5,
    WM_IPC_EVENT_GAINED_FOCUS = 6,
    WM_IPC_EVENT_LOST_FOCUS = 7,
    WM_IPC_EVENT_KBD = 8,
};

typedef struct wm_ipc_create {
    uint32_t req_id;
    uint32_t width;
    uint32_t height;
    uint32_t flags;
    uint32_t shm_id;
    char title[64];
} wm_ipc_create_t;

typedef struct wm_ipc_create_reply {
    uint32_t req_id;
    uint32_t win_id;
    int32_t x;
    int32_t y;
} wm_ipc_create_reply_t;

typedef struct wm_ipc_close {
    uint32_t win_id;
} wm_ipc_close_t;

// Damage rectangle uses window-local coordinates.
typedef struct wm_ipc_damage {
    uint32_t win_id;
    int32_t top;
    int32_t left;
    int32_t bottom;
    int32_t right;
} wm_ipc_damage_t;

typedef struct wm_ipc_event {
    uint32_t win_id;
    uint32_t type;
    int32_t x;
    int32_t y;
    uint8_t left;
    uint8_t right;
    uint8_t middle;
    uint8_t _pad;

    // Keyboard payload (used when type==WM_IPC_EVENT_KBD)
    uint32_t keycode;
    uint8_t pressed;
    char repr;
    uint8_t _pad2[2];
} wm_ipc_event_t;

#pragma once

#include "libc/stdint.h"
#include "libc/syscall.h"

#define WM_NORMAL 0
#define WM_BACKGROUND 1
#define WM_FOREGROUND 2
#define WM_SKIP_INPUT 4

#define WM_TB_HEIGHT 30

enum WM_CMD {
    WM_CMD_OPEN,
    WM_CMD_CLOSE,
    WM_CMD_RENDER,
    WM_CMD_INFO,
    WM_CMD_EVENT,
    WM_CMD_GET_POS,
    WM_CMD_IS_DRAGGED,
    WM_CMD_IS_HOVERED,
};

enum WM_EVENT {
    WM_EVENT_MOUSE_PRESS = 1,
    WM_EVENT_MOUSE_RELEASE,
    WM_EVENT_MOUSE_MOVE,
    WM_EVENT_MOUSE_ENTER,
    WM_EVENT_MOUSE_EXIT,
    WM_EVENT_KBD,
    WM_EVENT_GAINED_FOCUS,
    WM_EVENT_LOST_FOCUS,
    // Synthetic libgui event: user clicked window close button.
    WM_EVENT_CLOSE,
};

typedef struct {
    int32_t x, y;
} point_t;

typedef struct {
    int32_t top, left, bottom, right;
} wm_rect_t;

typedef struct {
    uintptr_t address;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
} fb_t;

typedef struct {
    wm_rect_t position;
    bool left_button;
    bool right_button;
} wm_click_event_t;

typedef struct {
    uint32_t keycode;
    bool pressed;
    char repr;
} wm_kbd_event_t;

typedef struct {
    uint32_t type;
    wm_click_event_t mouse;
    wm_kbd_event_t kbd;
} wm_event_t;

typedef struct {
    fb_t* fb;
    uint32_t flags;
} wm_param_open_t;

typedef struct {
    uint32_t win_id;
    wm_rect_t* clip;
} wm_param_render_t;

typedef struct {
    uint32_t win_id;
    wm_event_t* event;
} wm_param_event_t;

typedef struct {
    char* title;
    uint32_t width;
    uint32_t height;
    fb_t fb;
    uint32_t id;
    uint32_t flags;

    // Internal state used by libgui's /dev/wm backend.
    uint32_t shm_id;
    bool hovered_titlebar;
    int32_t last_mouse_x;
    int32_t last_mouse_y;
    bool last_left;
    bool last_right;
} window_t;

void get_fb_info(fb_t* fb);
void sleep(uint32_t ms);

// Drawing functions
void draw_pixel(fb_t fb, int x, int y, uint32_t col);
void draw_rect(fb_t fb, int x, int y, int w, int h, uint32_t col);
void draw_line(fb_t fb, int x0, int y0, int x1, int y1, uint32_t col);
void draw_border(fb_t fb, int x, int y, int w, int h, uint32_t col);
void draw_character(fb_t fb, char c, int x, int y, uint32_t col);
void draw_string(fb_t fb, char* str, int x, int y, uint32_t col);
void draw_rgba(fb_t fb, uint32_t* rgba, int x, int y, int w, int h);
void draw_rgb(fb_t fb, uint8_t* rgb, int x, int y, int w, int h);
void draw_rgb_masked(fb_t fb, uint8_t* rgb, int x, int y, int w, int h, uint32_t mask);

window_t* open_window(const char* title, int width, int height, uint32_t flags);
void close_window(window_t* win);
void draw_window(window_t* win);
void render_window(window_t* win);
void render_window_partial(window_t* win, wm_rect_t clip);
wm_event_t get_event(window_t* win);

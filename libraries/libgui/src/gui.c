#include "libgui/gui.h"

#include "libc/fbdev.h"
#include "libc/stdio.h"
#include "libc/stdlib.h"
#include "libc/string.h"
#include "libc/syscall.h"
#include "libc/wmdev.h"

static uint32_t g_req_id = 1;

static int wm_send(uint32_t dst, uint32_t type, const void* payload, uint32_t len) {
    uint8_t buf[sizeof(wm_send_hdr_t) + WM_MAX_PAYLOAD];
    if (len > WM_MAX_PAYLOAD) {
        return -1;
    }
    wm_send_hdr_t hdr = {.dst = dst, .type = type, .len = len};
    memcpy(buf, &hdr, sizeof(hdr));
    if (len && payload) {
        memcpy(buf + sizeof(hdr), payload, len);
    }
    int fd = sys_open("/dev/wm", O_WRONLY);
    if (fd < 0) {
        return -1;
    }
    int w = sys_write(fd, buf, (uint32_t) (sizeof(hdr) + len));
    sys_close(fd);
    return w;
}

static int wm_recv(uint32_t* out_src, uint32_t* out_type, void* payload, uint32_t* inout_len) {
    uint8_t buf[sizeof(wm_recv_hdr_t) + WM_MAX_PAYLOAD];
    int fd = sys_open("/dev/wm", O_RDONLY);
    if (fd < 0) {
        return -1;
    }
    int r = sys_read(fd, buf, (uint32_t) sizeof(buf));
    sys_close(fd);
    if (r <= 0) {
        return r;
    }
    if ((uint32_t) r < (uint32_t) sizeof(wm_recv_hdr_t)) {
        return -1;
    }
    wm_recv_hdr_t hdr;
    memcpy(&hdr, buf, sizeof(hdr));
    if (hdr.len > WM_MAX_PAYLOAD || (uint32_t) r != (uint32_t) (sizeof(hdr) + hdr.len)) {
        return -1;
    }
    if (out_src) {
        *out_src = hdr.src;
    }
    if (out_type) {
        *out_type = hdr.type;
    }
    if (inout_len) {
        uint32_t want = *inout_len;
        uint32_t n = hdr.len;
        if (want < n) {
            return -1;
        }
        if (payload && n) {
            memcpy(payload, buf + sizeof(hdr), n);
        }
        *inout_len = n;
    }
    return r;
}

static uint32_t wm_open_window_ipc(window_t* win) {
    uint32_t req = g_req_id++;
    wm_ipc_create_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.req_id = req;
    msg.width = win->width;
    msg.height = win->height;
    msg.flags = win->flags;
    msg.shm_id = win->shm_id;
    if (win->title) {
        strncpy(msg.title, win->title, sizeof(msg.title) - 1);
    }

    if (wm_send(0, WM_MSG_CREATE, &msg, (uint32_t) sizeof(msg)) < 0) {
        return 0;
    }

    for (int spins = 0; spins < 500; spins++) {
        uint32_t src = 0;
        uint32_t type = 0;
        wm_ipc_create_reply_t rep;
        uint32_t len = sizeof(rep);
        int r = wm_recv(&src, &type, &rep, &len);
        if (r == 0) {
            syscall1(SYS_SLEEP, 1);
            continue;
        }
        if (r < 0) {
            syscall1(SYS_SLEEP, 1);
            continue;
        }
        if (type == WM_MSG_CREATE_REPLY && len == sizeof(rep) && rep.req_id == req) {
            return rep.win_id;
        }
        // Drop unrelated messages (events from other windows etc.)
    }

    return 0;
}

static void wm_damage(window_t* win, wm_rect_t* clip) {
    if (!win || win->id == 0) {
        return;
    }
    wm_ipc_damage_t d;
    d.win_id = win->id;
    if (clip) {
        d.top = clip->top;
        d.left = clip->left;
        d.bottom = clip->bottom;
        d.right = clip->right;
    } else {
        d.top = 0;
        d.left = 0;
        d.bottom = (int32_t) win->height - 1;
        d.right = (int32_t) win->width - 1;
    }
    (void) wm_send(0, WM_MSG_DAMAGE, &d, (uint32_t) sizeof(d));
}

window_t* open_window(const char* title, int width, int height, uint32_t flags) {
    window_t* win = (window_t*) malloc(sizeof(window_t));
    uint32_t bpp = 32;

    if (!win) {
        return NULL;
    }

    // Support "fullscreen" like old demos by passing <=0.
    if (width <= 0 || height <= 0) {
        fb_t screen;
        get_fb_info(&screen);
        if (width <= 0) {
            width = (int) screen.width;
        }
        if (height <= 0) {
            height = (int) screen.height;
        }
    }

    win->title = strdup(title ? title : "");
    win->width = (uint32_t) width;
    win->height = (uint32_t) height;
    win->flags = flags;

    size_t bytes = (size_t) width * (size_t) height * (size_t) bpp / 8;
    int shm_id = syscall1(SYS_SHM_CREATE, (uint32_t) bytes);
    if (shm_id <= 0) {
        free(win->title);
        free(win);
        return NULL;
    }
    win->shm_id = (uint32_t) shm_id;
    void* addr = (void*) syscall1(SYS_SHM_MAP, win->shm_id);
    if (!addr) {
        (void) syscall1(SYS_SHM_CLOSE, win->shm_id);
        free(win->title);
        free(win);
        return NULL;
    }

    memset(addr, 0, bytes);

    win->fb = (fb_t) {
        .address = (uintptr_t) addr,
        .pitch = (uint32_t) width * bpp / 8,
        .width = (uint32_t) width,
        .height = (uint32_t) height,
        .bpp = bpp,
    };

    win->hovered_titlebar = false;
    win->last_mouse_x = 0;
    win->last_mouse_y = 0;
    win->last_left = false;
    win->last_right = false;

    win->id = wm_open_window_ipc(win);
    if (win->id == 0) {
        (void) syscall1(SYS_SHM_CLOSE, win->shm_id);
        free(win->title);
        free(win);
        return NULL;
    }

    return win;
}

void close_window(window_t* win) {
    if (!win) {
        return;
    }

    wm_ipc_close_t msg = {.win_id = win->id};
    (void) wm_send(0, WM_MSG_CLOSE, &msg, (uint32_t) sizeof(msg));

    free(win->title);
    (void) syscall1(SYS_SHM_CLOSE, win->shm_id);
    free(win);
}

void draw_window(window_t* win) {
    uint32_t bg_color = 0x1E1E2E;
    uint32_t base_color = 0x282A36;
    uint32_t highlight = 0x44475A;
    uint32_t border_color = 0x6272A4;
    uint32_t text_color = 0xF8F8F2;
    uint32_t border_color2 = 0x44475A;

    if (!win) {
        return;
    }

    bool titlebar_hovered = win->hovered_titlebar;

    draw_rect(win->fb, 0, 0, (int) win->width, (int) win->height, bg_color);

    if (titlebar_hovered) {
        draw_rect(win->fb, 0, 0, (int) win->width, WM_TB_HEIGHT, base_color + highlight);
        draw_border(win->fb, 0, 0, (int) win->width, WM_TB_HEIGHT, highlight);
    } else {
        draw_rect(win->fb, 0, 0, (int) win->width, WM_TB_HEIGHT, base_color);
        draw_border(win->fb, 0, 0, (int) win->width, WM_TB_HEIGHT, border_color);
    }

    draw_string(win->fb, win->title, 8, WM_TB_HEIGHT / 3, text_color);

    // Close button (top-right). Clients can treat a click here as WM_EVENT_CLOSE.
    int bx = (int) win->width - 24;
    int by = 6;
    bool close_hover = (win->last_mouse_y >= by && win->last_mouse_y < by + 18
                        && win->last_mouse_x >= bx && win->last_mouse_x < bx + 18);
    uint32_t btn_col = close_hover ? (base_color + highlight) : base_color;
    draw_rect(win->fb, bx, by, 18, 18, btn_col);
    draw_border(win->fb, bx, by, 18, 18, border_color);
    draw_string(win->fb, (char*) "X", bx + 6, by + 4, text_color);

    draw_border(win->fb, 0, 0, (int) win->width, (int) win->height, border_color2);
}

void render_window(window_t* win) {
    wm_damage(win, NULL);
}

void render_window_partial(window_t* win, wm_rect_t clip) {
    wm_damage(win, &clip);
}

wm_event_t get_event(window_t* win) {
    wm_event_t event;
    memset(&event, 0, sizeof(event));
    if (!win || win->id == 0) {
        return event;
    }

    for (;;) {
        uint32_t src = 0;
        uint32_t type = 0;
        wm_ipc_event_t ev;
        uint32_t len = sizeof(ev);
        int r = wm_recv(&src, &type, &ev, &len);
        if (r <= 0) {
            return event;
        }
        if (type != WM_MSG_EVENT || len != sizeof(ev)) {
            continue;
        }
        if (ev.win_id != win->id) {
            continue;
        }

        // Track mouse state for hover/highlight.
        win->last_mouse_x = ev.x;
        win->last_mouse_y = ev.y;
        win->last_left = ev.left ? true : false;
        win->last_right = ev.right ? true : false;
        win->hovered_titlebar = (ev.y >= 0 && ev.y < WM_TB_HEIGHT);

        event.type = ev.type;
        if (ev.type >= WM_IPC_EVENT_MOUSE_PRESS && ev.type <= WM_IPC_EVENT_MOUSE_EXIT) {
            event.mouse.position.top = ev.y;
            event.mouse.position.left = ev.x;
            event.mouse.position.bottom = ev.y + 16;
            event.mouse.position.right = ev.x + 16;
            event.mouse.left_button = ev.left ? true : false;
            event.mouse.right_button = ev.right ? true : false;

            // Synthesize close-button click.
            if (ev.type == WM_IPC_EVENT_MOUSE_PRESS && ev.left) {
                int bx = (int) win->width - 24;
                int by = 6;
                if (ev.y >= by && ev.y < by + 18 && ev.x >= bx && ev.x < bx + 18) {
                    event.type = WM_EVENT_CLOSE;
                }
            }
        }
        if (ev.type == WM_IPC_EVENT_GAINED_FOCUS) {
            event.type = WM_EVENT_GAINED_FOCUS;
        }
        if (ev.type == WM_IPC_EVENT_LOST_FOCUS) {
            event.type = WM_EVENT_LOST_FOCUS;
        }

        if (ev.type == WM_IPC_EVENT_KBD) {
            event.type = WM_EVENT_KBD;
            event.kbd.keycode = ev.keycode;
            event.kbd.pressed = ev.pressed ? true : false;
            event.kbd.repr = ev.repr;
        }

        return event;
    }
}

void get_fb_info(fb_t* fb) {
    if (!fb) {
        return;
    }
    fbdev_info_t info;
    int fd = sys_open("/dev/fb0", O_RDONLY);
    if (fd < 0) {
        memset(fb, 0, sizeof(*fb));
        return;
    }
    int r = sys_read(fd, &info, sizeof(info));
    sys_close(fd);
    if (r != (int) sizeof(info)) {
        memset(fb, 0, sizeof(*fb));
        return;
    }
    fb->address = 0;
    fb->pitch = info.pitch;
    fb->width = info.width;
    fb->height = info.height;
    fb->bpp = info.bpp;
}

void sleep(uint32_t ms) {
    syscall1(SYS_SLEEP, ms);
}

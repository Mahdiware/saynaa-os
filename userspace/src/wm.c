#include "libc/fbdev.h"
#include "libc/mouse.h"
#include "libc/stdio.h"
#include "libc/stdlib.h"
#include "libc/string.h"
#include "libc/syscall.h"
#include "libc/wmdev.h"

// Must match kernel/include/fs/dev/dev_kbd.h
typedef struct kbd_event {
    uint32_t keycode;
    uint8_t pressed;
    char repr;
    uint8_t _pad;
} kbd_event_t;

#define MAX_WINS 32
#define MOUSE_SIZE 16
#define WM_TB_HEIGHT 30

typedef struct rect {
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
} rect_t;

typedef struct wm_win {
    uint32_t in_use;
    uint32_t win_id;
    uint32_t owner_pid;
    uint32_t flags;
    uint32_t w;
    uint32_t h;
    uint32_t pitch_px;
    uint32_t shm_id;
    uint32_t* buf;
    int32_t x;
    int32_t y;
} wm_win_t;

static wm_win_t g_wins[MAX_WINS];
static uint32_t g_next_win_id = 1;
static uint32_t g_screen_w;
static uint32_t g_screen_h;
static uint32_t g_pitch_px;
static uint32_t g_fb_off;
static uint32_t* g_back;

static int32_t g_mx;
static int32_t g_my;
static uint8_t g_left;
static uint8_t g_right;
static uint8_t g_middle;

static int32_t g_drag_dx;
static int32_t g_drag_dy;
static uint32_t g_drag_win; // win_id
static uint8_t g_dragging;

static uint32_t g_hover_win;
static uint32_t g_focus_win;

static void fb_fill(uint32_t* fb, uint32_t pitch_px, uint32_t w, uint32_t h, uint32_t col) {
    for (uint32_t y = 0; y < h; y++) {
        uint32_t* row = fb + y * pitch_px;
        for (uint32_t x = 0; x < w; x++) {
            row[x] = col;
        }
    }
}

static void fb_draw_cursor(uint32_t* fb, uint32_t pitch_px, uint32_t w, uint32_t h, int32_t x0, int32_t y0) {
    uint32_t col = 0xffffffff;
    for (int32_t y = 0; y < 12; y++) {
        int32_t sy = y0 + y;
        if (sy < 0 || sy >= (int32_t) h)
            continue;
        int32_t span = y + 1;
        if (span > 10)
            span = 10;
        uint32_t* row = fb + (uint32_t) sy * pitch_px;
        for (int32_t x = 0; x < span; x++) {
            int32_t sx = x0 + x;
            if (sx < 0 || sx >= (int32_t) w)
                continue;
            row[(uint32_t) sx] = col;
        }
    }
}

static void fb_draw_cursor_clipped(uint32_t* fb, uint32_t pitch_px, uint32_t w, uint32_t h,
    int32_t x0, int32_t y0, int32_t clip_x, int32_t clip_y, int32_t clip_w, int32_t clip_h) {
    int32_t clip_x1 = clip_x + clip_w;
    int32_t clip_y1 = clip_y + clip_h;

    uint32_t col = 0xffffffff;
    for (int32_t y = 0; y < 12; y++) {
        int32_t sy = y0 + y;
        if (sy < 0 || sy >= (int32_t) h)
            continue;
        if (sy < clip_y || sy >= clip_y1)
            continue;
        int32_t span = y + 1;
        if (span > 10)
            span = 10;
        uint32_t* row = fb + (uint32_t) sy * pitch_px;
        for (int32_t x = 0; x < span; x++) {
            int32_t sx = x0 + x;
            if (sx < 0 || sx >= (int32_t) w)
                continue;
            if (sx < clip_x || sx >= clip_x1)
                continue;
            row[(uint32_t) sx] = col;
        }
    }
}

static rect_t rect_union(rect_t a, rect_t b) {
    int32_t x0 = a.x < b.x ? a.x : b.x;
    int32_t y0 = a.y < b.y ? a.y : b.y;
    int32_t x1a = a.x + a.w;
    int32_t y1a = a.y + a.h;
    int32_t x1b = b.x + b.w;
    int32_t y1b = b.y + b.h;
    int32_t x1 = x1a > x1b ? x1a : x1b;
    int32_t y1 = y1a > y1b ? y1a : y1b;
    rect_t r;
    r.x = x0;
    r.y = y0;
    r.w = x1 - x0;
    r.h = y1 - y0;
    return r;
}

static wm_win_t* win_by_id(uint32_t id) {
    for (uint32_t i = 0; i < MAX_WINS; i++) {
        if (g_wins[i].in_use && g_wins[i].win_id == id) {
            return &g_wins[i];
        }
    }
    return NULL;
}

static uint32_t win_at(int32_t x, int32_t y) {
    uint32_t best = 0;
    for (uint32_t i = 0; i < MAX_WINS; i++) {
        if (!g_wins[i].in_use)
            continue;
        wm_win_t* w = &g_wins[i];
        if (w->flags & 4) {
            // WM_SKIP_INPUT
            continue;
        }
        if (x >= w->x && x < w->x + (int32_t) w->w && y >= w->y && y < w->y + (int32_t) w->h) {
            // later slots treated as topmost; keep last match
            best = w->win_id;
        }
    }
    return best;
}

static bool win_is_focusable(const wm_win_t* w) {
    if (!w) {
        return false;
    }
    if (w->flags & 4) {
        // WM_SKIP_INPUT
        return false;
    }
    if (w->flags & 1) {
        // WM_BACKGROUND
        return false;
    }
    if (w->flags & 2) {
        // WM_FOREGROUND
        return false;
    }
    return true;
}

static wm_win_t* topmost_focusable_window(void) {
    wm_win_t* best = NULL;
    for (uint32_t i = 0; i < MAX_WINS; i++) {
        if (!g_wins[i].in_use) {
            continue;
        }
        if (!win_is_focusable(&g_wins[i])) {
            continue;
        }
        // Later slots are treated as topmost.
        best = &g_wins[i];
    }
    return best;
}

static void raise_window(uint32_t id) {
    if (!id) {
        return;
    }
    // Keep WM_BACKGROUND at bottom and WM_FOREGROUND at top.
    wm_win_t* w = win_by_id(id);
    if (!w) {
        return;
    }
    if (w->flags & 1) {
        // WM_BACKGROUND
        return;
    }
    if (w->flags & 2) {
        // WM_FOREGROUND
        return;
    }

    // Bubble the window toward the end by swapping slots.
    for (uint32_t i = 0; i + 1 < MAX_WINS; i++) {
        if (g_wins[i].in_use && g_wins[i].win_id == id) {
            // find next occupied slot that is not FOREGROUND
            for (uint32_t j = i + 1; j < MAX_WINS; j++) {
                if (!g_wins[j].in_use) {
                    // move to empty slot
                    g_wins[j] = g_wins[i];
                    memset(&g_wins[i], 0, sizeof(g_wins[i]));
                    return;
                }
                if (g_wins[j].in_use && (g_wins[j].flags & 2)) {
                    // insert before first foreground window by swapping
                    wm_win_t tmp = g_wins[j - 1];
                    g_wins[j - 1] = g_wins[i];
                    g_wins[i] = tmp;
                    return;
                }
            }
        }
    }
}

static int wm_send_to(uint32_t dst, uint32_t type, const void* payload, uint32_t len) {
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
    sys_seek(fd, 0, SYS_SEEK_SET);
    int w = sys_write(fd, buf, (uint32_t) (sizeof(hdr) + len));
    sys_close(fd);
    return w;
}

static int wm_recv_msg(uint32_t* out_src, uint32_t* out_type, void* payload, uint32_t* inout_len) {
    uint8_t buf[sizeof(wm_recv_hdr_t) + WM_MAX_PAYLOAD];
    int fd = sys_open("/dev/wm", O_RDONLY);
    if (fd < 0) {
        return -1;
    }
    sys_seek(fd, 0, SYS_SEEK_SET);
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
    if (out_src)
        *out_src = hdr.src;
    if (out_type)
        *out_type = hdr.type;
    if (inout_len) {
        if (*inout_len < hdr.len) {
            return -1;
        }
        if (payload && hdr.len) {
            memcpy(payload, buf + sizeof(hdr), hdr.len);
        }
        *inout_len = hdr.len;
    }
    return r;
}

static void fb_blit_rect(uint32_t* src, int32_t x, int32_t y, int32_t w, int32_t h) {
    if (w <= 0 || h <= 0) {
        return;
    }
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > (int32_t) g_screen_w) {
        w = (int32_t) g_screen_w - x;
    }
    if (y + h > (int32_t) g_screen_h) {
        h = (int32_t) g_screen_h - y;
    }
    if (w <= 0 || h <= 0) {
        return;
    }

    uint32_t bytes_per_row = (uint32_t) w * 4;
    int fd = sys_open("/dev/fb0", O_WRONLY);
    if (fd < 0) {
        return;
    }
    for (int32_t row = 0; row < h; row++) {
        uint32_t off = g_fb_off + (uint32_t) (y + row) * (g_pitch_px * 4) + (uint32_t) x * 4;
        const uint32_t* p = src + (uint32_t) (y + row) * g_pitch_px + (uint32_t) x;
        sys_seek(fd, (int32_t) off, SYS_SEEK_SET);
        (void) sys_write(fd, (const void*) p, bytes_per_row);
    }
    sys_close(fd);
}

static void composite_full(void) {
    fb_fill(g_back, g_pitch_px, g_screen_w, g_screen_h, 0xff0b1020);

    for (uint32_t i = 0; i < MAX_WINS; i++) {
        if (!g_wins[i].in_use)
            continue;
        wm_win_t* w = &g_wins[i];
        if (!w->buf)
            continue;

        int32_t sx0 = w->x;
        int32_t sy0 = w->y;
        int32_t sx1 = sx0 + (int32_t) w->w;
        int32_t sy1 = sy0 + (int32_t) w->h;

        int32_t dx0 = sx0 < 0 ? 0 : sx0;
        int32_t dy0 = sy0 < 0 ? 0 : sy0;
        int32_t dx1 = sx1 > (int32_t) g_screen_w ? (int32_t) g_screen_w : sx1;
        int32_t dy1 = sy1 > (int32_t) g_screen_h ? (int32_t) g_screen_h : sy1;
        if (dx1 <= dx0 || dy1 <= dy0)
            continue;

        int32_t copy_w = dx1 - dx0;
        int32_t copy_h = dy1 - dy0;
        int32_t src_x = dx0 - sx0;
        int32_t src_y = dy0 - sy0;

        for (int32_t yy = 0; yy < copy_h; yy++) {
            uint32_t* dst = g_back + (uint32_t) (dy0 + yy) * g_pitch_px + (uint32_t) dx0;
            uint32_t* src = w->buf + (uint32_t) (src_y + yy) * w->pitch_px + (uint32_t) src_x;
            memcpy(dst, src, (size_t) copy_w * 4);
        }
    }

    fb_draw_cursor(g_back, g_pitch_px, g_screen_w, g_screen_h, g_mx, g_my);
    fb_blit_rect(g_back, 0, 0, (int32_t) g_screen_w, (int32_t) g_screen_h);
}

static void composite_rect(int32_t x, int32_t y, int32_t w, int32_t h) {
    if (w <= 0 || h <= 0) {
        return;
    }
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > (int32_t) g_screen_w) {
        w = (int32_t) g_screen_w - x;
    }
    if (y + h > (int32_t) g_screen_h) {
        h = (int32_t) g_screen_h - y;
    }
    if (w <= 0 || h <= 0) {
        return;
    }

    // Fill background only in dirty rect.
    for (int32_t yy = 0; yy < h; yy++) {
        uint32_t* row = g_back + (uint32_t) (y + yy) * g_pitch_px + (uint32_t) x;
        for (int32_t xx = 0; xx < w; xx++) {
            row[(uint32_t) xx] = 0xff0b1020;
        }
    }

    // Composite windows intersecting the dirty rect.
    int32_t rx0 = x;
    int32_t ry0 = y;
    int32_t rx1 = x + w;
    int32_t ry1 = y + h;

    for (uint32_t i = 0; i < MAX_WINS; i++) {
        if (!g_wins[i].in_use)
            continue;
        wm_win_t* win = &g_wins[i];
        if (!win->buf)
            continue;

        int32_t wx0 = win->x;
        int32_t wy0 = win->y;
        int32_t wx1 = wx0 + (int32_t) win->w;
        int32_t wy1 = wy0 + (int32_t) win->h;

        int32_t ox0 = rx0 > wx0 ? rx0 : wx0;
        int32_t oy0 = ry0 > wy0 ? ry0 : wy0;
        int32_t ox1 = rx1 < wx1 ? rx1 : wx1;
        int32_t oy1 = ry1 < wy1 ? ry1 : wy1;

        if (ox1 <= ox0 || oy1 <= oy0)
            continue;

        int32_t copy_w = ox1 - ox0;
        int32_t copy_h = oy1 - oy0;
        int32_t src_x = ox0 - wx0;
        int32_t src_y = oy0 - wy0;

        for (int32_t yy = 0; yy < copy_h; yy++) {
            uint32_t* dst = g_back + (uint32_t) (oy0 + yy) * g_pitch_px + (uint32_t) ox0;
            uint32_t* src = win->buf + (uint32_t) (src_y + yy) * win->pitch_px + (uint32_t) src_x;
            memcpy(dst, src, (size_t) copy_w * 4);
        }
    }

    // Cursor overlay (clipped to dirty rect).
    fb_draw_cursor_clipped(g_back, g_pitch_px, g_screen_w, g_screen_h, g_mx, g_my, x, y, w, h);

    fb_blit_rect(g_back, x, y, w, h);
}

static void send_event_to(uint32_t pid, const wm_ipc_event_t* ev) {
    if (!pid || !ev) {
        return;
    }
    (void) wm_send_to(pid, WM_MSG_EVENT, ev, (uint32_t) sizeof(*ev));
}

static void update_focus(uint32_t new_focus) {
    if (new_focus == g_focus_win) {
        return;
    }
    wm_win_t* oldw = win_by_id(g_focus_win);
    if (oldw) {
        wm_ipc_event_t e;
        memset(&e, 0, sizeof(e));
        e.win_id = oldw->win_id;
        e.type = WM_IPC_EVENT_LOST_FOCUS;
        send_event_to(oldw->owner_pid, &e);
    }
    g_focus_win = new_focus;
    wm_win_t* nw = win_by_id(g_focus_win);
    if (nw) {
        wm_ipc_event_t e;
        memset(&e, 0, sizeof(e));
        e.win_id = nw->win_id;
        e.type = WM_IPC_EVENT_GAINED_FOCUS;
        send_event_to(nw->owner_pid, &e);
    }
}

static void handle_mouse_event(void) {
    uint32_t under = win_at(g_mx, g_my);
    if (under != g_hover_win) {
        if (g_hover_win) {
            wm_win_t* w = win_by_id(g_hover_win);
            if (w) {
                wm_ipc_event_t ex;
                memset(&ex, 0, sizeof(ex));
                ex.win_id = w->win_id;
                ex.type = WM_IPC_EVENT_MOUSE_EXIT;
                send_event_to(w->owner_pid, &ex);
            }
        }
        if (under) {
            wm_win_t* w = win_by_id(under);
            if (w) {
                wm_ipc_event_t en;
                memset(&en, 0, sizeof(en));
                en.win_id = w->win_id;
                en.type = WM_IPC_EVENT_MOUSE_ENTER;
                en.x = g_mx - w->x;
                en.y = g_my - w->y;
                send_event_to(w->owner_pid, &en);
            }
        }
        g_hover_win = under;
    }

    if (under) {
        wm_win_t* w = win_by_id(under);
        if (w) {
            wm_ipc_event_t mv;
            memset(&mv, 0, sizeof(mv));
            mv.win_id = w->win_id;
            mv.type = WM_IPC_EVENT_MOUSE_MOVE;
            mv.x = g_mx - w->x;
            mv.y = g_my - w->y;
            mv.left = g_left;
            mv.right = g_right;
            mv.middle = g_middle;
            send_event_to(w->owner_pid, &mv);
        }
    }
}

int main(void) {
    fbdev_info_t info;
    int fb = sys_open("/dev/fb0", O_RDONLY);
    int r = (fb >= 0) ? sys_read(fb, &info, sizeof(info)) : -1;
    if (fb >= 0) {
        sys_close(fb);
    }
    if (r != (int) sizeof(info) || info.bpp != 32 || info.pitch == 0) {
        printf("wm: fb0 read failed (r=%d bpp=%u)\n", r, info.bpp);
        return 1;
    }

    g_screen_w = info.width;
    g_screen_h = info.height;
    g_pitch_px = info.pitch / 4;
    g_fb_off = (uint32_t) sizeof(fbdev_info_t);

    g_back = (uint32_t*) malloc((size_t) info.size_bytes);
    if (!g_back) {
        printf("wm: out of memory\n");
        return 1;
    }

    memset(g_wins, 0, sizeof(g_wins));
    g_mx = (int32_t) g_screen_w / 2;
    g_my = (int32_t) g_screen_h / 2;

    // Become /dev/wm server (first writer).
    (void) wm_send_to(0, WM_MSG_NOP, NULL, 0);

    composite_full();

    int mouse_fd = sys_open("/dev/mouse0", O_RDONLY);
    int kbd_fd = sys_open("/dev/kbd0", O_RDONLY);

    for (;;) {
        // Drain client->server messages.
        for (;;) {
            uint32_t src = 0;
            uint32_t type = 0;
            uint8_t payload[WM_MAX_PAYLOAD];
            uint32_t len = sizeof(payload);
            int rr = wm_recv_msg(&src, &type, payload, &len);
            if (rr == 0) {
                break;
            }
            if (rr < 0) {
                break;
            }

            if (type == WM_MSG_CREATE && len == sizeof(wm_ipc_create_t)) {
                wm_ipc_create_t* c = (wm_ipc_create_t*) payload;
                // Find slot
                wm_win_t* slot = NULL;
                for (uint32_t i = 0; i < MAX_WINS; i++) {
                    if (!g_wins[i].in_use) {
                        slot = &g_wins[i];
                        break;
                    }
                }
                if (!slot) {
                    // Reply with win_id=0
                    wm_ipc_create_reply_t rep;
                    rep.req_id = c->req_id;
                    rep.win_id = 0;
                    rep.x = 0;
                    rep.y = 0;
                    (void) wm_send_to(src, WM_MSG_CREATE_REPLY, &rep, (uint32_t) sizeof(rep));
                    continue;
                }

                void* buf = (void*) syscall1(SYS_SHM_MAP, c->shm_id);
                if (!buf) {
                    wm_ipc_create_reply_t rep;
                    rep.req_id = c->req_id;
                    rep.win_id = 0;
                    rep.x = 0;
                    rep.y = 0;
                    (void) wm_send_to(src, WM_MSG_CREATE_REPLY, &rep, (uint32_t) sizeof(rep));
                    continue;
                }

                uint32_t id = g_next_win_id++;
                if (id == 0) {
                    id = g_next_win_id++;
                }

                int32_t x = 10 + (int32_t) ((id * 24) % 200);
                int32_t y = 40 + (int32_t) ((id * 18) % 140);
                if (c->flags & 1) {
                    x = 0;
                    y = 0;
                }
                if (c->flags & 2) {
                    x = 0;
                    y = 0;
                }

                memset(slot, 0, sizeof(*slot));
                slot->in_use = 1;
                slot->win_id = id;
                slot->owner_pid = src;
                slot->flags = c->flags;
                slot->w = c->width;
                slot->h = c->height;
                slot->pitch_px = c->width;
                slot->shm_id = c->shm_id;
                slot->buf = (uint32_t*) buf;
                slot->x = x;
                slot->y = y;

                // Raise behavior: foreground should end up last.
                if (slot->flags & 2) {
                    // move to last slot by swapping with later empties
                    for (uint32_t i = 0; i + 1 < MAX_WINS; i++) {
                        if (g_wins[i].in_use && g_wins[i].win_id == id) {
                            // find last free slot
                            for (int j = (int) MAX_WINS - 1; j > (int) i; j--) {
                                if (!g_wins[j].in_use) {
                                    g_wins[j] = g_wins[i];
                                    memset(&g_wins[i], 0, sizeof(g_wins[i]));
                                    break;
                                }
                            }
                            break;
                        }
                    }
                }

                wm_ipc_create_reply_t rep;
                rep.req_id = c->req_id;
                rep.win_id = id;
                rep.x = x;
                rep.y = y;
                (void) wm_send_to(src, WM_MSG_CREATE_REPLY, &rep, (uint32_t) sizeof(rep));

                raise_window(id);
                if (win_is_focusable(win_by_id(id))) {
                    update_focus(id);
                }
                composite_full();
            } else if (type == WM_MSG_CLOSE && len == sizeof(wm_ipc_close_t)) {
                wm_ipc_close_t* c = (wm_ipc_close_t*) payload;
                wm_win_t* w = win_by_id(c->win_id);
                if (w && w->owner_pid == src) {
                    bool was_focused = (g_focus_win == c->win_id);
                    if (was_focused) {
                        // Notify the old focused window before destroying it.
                        update_focus(0);
                    }
                    (void) syscall1(SYS_SHM_CLOSE, w->shm_id);
                    memset(w, 0, sizeof(*w));
                    if (g_hover_win == c->win_id) {
                        g_hover_win = 0;
                    }
                    if (g_drag_win == c->win_id) {
                        g_drag_win = 0;
                        g_dragging = 0;
                    }

                    if (was_focused) {
                        wm_win_t* next = topmost_focusable_window();
                        update_focus(next ? next->win_id : 0);
                    }
                    composite_full();
                }
            } else if (type == WM_MSG_DAMAGE && len == sizeof(wm_ipc_damage_t)) {
                wm_ipc_damage_t* d = (wm_ipc_damage_t*) payload;
                wm_win_t* w = win_by_id(d->win_id);
                if (w) {
                    int32_t x0 = w->x + d->left;
                    int32_t y0 = w->y + d->top;
                    int32_t x1 = w->x + d->right;
                    int32_t y1 = w->y + d->bottom;
                    composite_rect(x0, y0, x1 - x0, y1 - y0);
                }
            }
        }

        // Mouse input.
        mouse_event_t mev;
        int mn = -1;
        if (mouse_fd >= 0) {
            sys_seek(mouse_fd, 0, SYS_SEEK_SET);
            mn = sys_read(mouse_fd, &mev, sizeof(mev));
        }
        if (mn == (int) sizeof(mev)) {
            int32_t oldx = g_mx;
            int32_t oldy = g_my;
            uint8_t old_left = g_left;
            bool did_redraw = false;

            g_mx = mev.x;
            g_my = mev.y;
            g_left = (mev.buttons & 0x1) ? 1 : 0;
            g_right = (mev.buttons & 0x2) ? 1 : 0;
            g_middle = (mev.buttons & 0x4) ? 1 : 0;

            uint8_t moved = (oldx != g_mx) || (oldy != g_my);
            uint8_t left_down = (!old_left && g_left);
            uint8_t left_up = (old_left && !g_left);

            if (left_down) {
                uint32_t under = win_at(g_mx, g_my);
                if (under) {
                    raise_window(under);
                    wm_win_t* w = win_by_id(under);
                    if (w) {
                        if (win_is_focusable(w)) {
                            update_focus(under);
                        }
                        int32_t lx = g_mx - w->x;
                        int32_t ly = g_my - w->y;
                        // Drag only when in titlebar and not foreground.
                        if (!(w->flags & 2) && ly >= 0 && ly < WM_TB_HEIGHT) {
                            g_dragging = 1;
                            g_drag_win = under;
                            g_drag_dx = lx;
                            g_drag_dy = ly;
                        }
                        wm_ipc_event_t ev;
                        memset(&ev, 0, sizeof(ev));
                        ev.win_id = w->win_id;
                        ev.type = WM_IPC_EVENT_MOUSE_PRESS;
                        ev.x = lx;
                        ev.y = ly;
                        ev.left = g_left;
                        ev.right = g_right;
                        ev.middle = g_middle;
                        send_event_to(w->owner_pid, &ev);
                    }
                }
                composite_full();
                did_redraw = true;
            }

            if (g_dragging && g_drag_win && g_left && moved) {
                wm_win_t* w = win_by_id(g_drag_win);
                if (w) {
                    int32_t oldwx = w->x;
                    int32_t oldwy = w->y;
                    int32_t nx = g_mx - g_drag_dx;
                    int32_t ny = g_my - g_drag_dy;
                    if (nx < 0)
                        nx = 0;
                    if (ny < 0)
                        ny = 0;
                    int32_t maxx = (int32_t) g_screen_w - (int32_t) w->w;
                    int32_t maxy = (int32_t) g_screen_h - (int32_t) w->h;
                    if (maxx < 0)
                        maxx = 0;
                    if (maxy < 0)
                        maxy = 0;
                    if (nx > maxx)
                        nx = maxx;
                    if (ny > maxy)
                        ny = maxy;
                    if (nx != w->x || ny != w->y) {
                        w->x = nx;
                        w->y = ny;
                        rect_t a = {.x = oldwx, .y = oldwy, .w = (int32_t) w->w, .h = (int32_t) w->h};
                        rect_t b = {.x = w->x, .y = w->y, .w = (int32_t) w->w, .h = (int32_t) w->h};
                        rect_t dirty = rect_union(a, b);
                        rect_t c0 = {.x = oldx, .y = oldy, .w = MOUSE_SIZE, .h = MOUSE_SIZE};
                        rect_t c1 = {.x = g_mx, .y = g_my, .w = MOUSE_SIZE, .h = MOUSE_SIZE};
                        dirty = rect_union(dirty, rect_union(c0, c1));
                        composite_rect(dirty.x, dirty.y, dirty.w, dirty.h);
                        did_redraw = true;
                    }
                }
            }

            if (left_up) {
                if (g_dragging) {
                    g_dragging = 0;
                    g_drag_win = 0;
                }
                uint32_t under = win_at(g_mx, g_my);
                wm_win_t* w = under ? win_by_id(under) : NULL;
                if (w) {
                    wm_ipc_event_t ev;
                    memset(&ev, 0, sizeof(ev));
                    ev.win_id = w->win_id;
                    ev.type = WM_IPC_EVENT_MOUSE_RELEASE;
                    ev.x = g_mx - w->x;
                    ev.y = g_my - w->y;
                    ev.left = g_left;
                    ev.right = g_right;
                    ev.middle = g_middle;
                    send_event_to(w->owner_pid, &ev);
                }
                composite_full();
                did_redraw = true;
            }

            if (moved) {
                handle_mouse_event();
                if (!did_redraw) {
                    rect_t c0 = {.x = oldx, .y = oldy, .w = MOUSE_SIZE, .h = MOUSE_SIZE};
                    rect_t c1 = {.x = g_mx, .y = g_my, .w = MOUSE_SIZE, .h = MOUSE_SIZE};
                    rect_t dirty = rect_union(c0, c1);
                    composite_rect(dirty.x, dirty.y, dirty.w, dirty.h);
                }
            }
        }

        // Keyboard input (non-blocking /dev/kbd0).
        for (;;) {
            kbd_event_t kev;
            int kn = -1;
            if (kbd_fd >= 0) {
                sys_seek(kbd_fd, 0, SYS_SEEK_SET);
                kn = sys_read(kbd_fd, &kev, (uint32_t) sizeof(kev));
            }
            if (kn != (int) sizeof(kev)) {
                break;
            }

            wm_win_t* fw = win_by_id(g_focus_win);
            if (!fw || !win_is_focusable(fw)) {
                // Recover focus: prefer window under mouse, else topmost normal window.
                uint32_t under = win_at(g_mx, g_my);
                wm_win_t* uw = under ? win_by_id(under) : NULL;
                if (uw && win_is_focusable(uw)) {
                    update_focus(uw->win_id);
                    fw = uw;
                } else {
                    wm_win_t* tw = topmost_focusable_window();
                    if (tw) {
                        update_focus(tw->win_id);
                        fw = tw;
                    }
                }
            }

            if (fw) {
                wm_ipc_event_t e;
                memset(&e, 0, sizeof(e));
                e.win_id = fw->win_id;
                e.type = WM_IPC_EVENT_KBD;
                e.keycode = kev.keycode;
                e.pressed = kev.pressed;
                e.repr = kev.repr;
                send_event_to(fw->owner_pid, &e);
            }
        }

        syscall1(SYS_SLEEP, 5);
    }
}

#include "libgui/gui.h"

#include "libc/stdint.h"

static void draw_content(window_t* win, uint32_t theme_bg, uint32_t accent) {
    if (!win) {
        return;
    }

    // Decorated frame (titlebar + border)
    draw_window(win);

    // Client area
    int x0 = 8;
    int y0 = WM_TB_HEIGHT + 8;
    int w = (int) win->width - 16;
    int h = (int) win->height - y0 - 8;
    if (w < 1)
        w = 1;
    if (h < 1)
        h = 1;

    draw_rect(win->fb, x0, y0, w, h, theme_bg);
    draw_border(win->fb, x0, y0, w, h, 0xff202020);

    // Accent bar inside client area
    draw_rect(win->fb, x0 + 1, y0 + 1, w - 2, 24, accent);
    draw_line(win->fb, x0 + 1, y0 + 25, x0 + w - 2, y0 + 25, 0xff202020);

    char hint[] = "Keys: 1/2/3 theme, Q quit";
    draw_string(win->fb, hint, x0 + 10, y0 + 8, 0xffffffff);
}

int main(int argc, char** argv) {
    (void) argc;
    (void) argv;

    window_t* win = open_window("gui", 520, 360, WM_NORMAL);
    if (!win) {
        return 1;
    }

    uint32_t theme_bg = 0xff0b1020;
    uint32_t accent = 0xff1d2a4a;

    draw_content(win, theme_bg, accent);
    render_window(win);

    for (;;) {
        wm_event_t ev = get_event(win);
        if (ev.type == 0) {
            sleep(10);
            continue;
        }
        if (ev.type != WM_EVENT_KBD) {
            continue;
        }

        char c = ev.kbd.repr;
        if (c == 'q' || c == 'Q') {
            break;
        }

        if (c == '1') {
            theme_bg = 0xff0b1020;
            accent = 0xff1d2a4a;
        } else if (c == '2') {
            theme_bg = 0xff101018;
            accent = 0xff2a7b6a;
        } else if (c == '3') {
            theme_bg = 0xff1a0f14;
            accent = 0xff8a2be2;
        } else {
            continue;
        }

        draw_content(win, theme_bg, accent);
        render_window(win);
    }

    close_window(win);
    return 0;
}

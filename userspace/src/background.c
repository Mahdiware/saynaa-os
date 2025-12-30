#include "libc/stdint.h"
#include "libc/syscall.h"
#include "libgui/gui.h"

int main(int argc, char** argv) {
    (void) argc;
    (void) argv;

    fb_t screen;
    get_fb_info(&screen);

    // Fullscreen background window (no input).
    window_t* bg = open_window("background", (int) screen.width, (int) screen.height, WM_BACKGROUND | WM_SKIP_INPUT);
    if (!bg) {
        sys_write(1, "background: window manager is not running\n", 44);
        return 1;
    }

    // Foreground top bar window (receives input, stays on top).
    const int bar_h = WM_TB_HEIGHT;
    window_t* bar = open_window("bar", (int) screen.width, bar_h, WM_FOREGROUND);
    if (!bar) {
        sys_write(1, "background: window manager is not running\n", 44);
        close_window(bg);
        return 1;
    }

    // Colors
    uint32_t base = 0xff0b1020;
    uint32_t bar_col = 0xff1d2a4a;
    uint32_t btn_bg = 0xff24365f;
    uint32_t btn_border = 0xff3a4f86;
    uint32_t btn_text = 0xfff0f0f0;

    // Button geometry
    const int btn_x = 8;
    const int btn_y = 6;
    const int btn_w = 90;
    const int btn_h = bar_h - 12;

    // Draw background once.
    draw_rect(bg->fb, 0, 0, (int) bg->width, (int) bg->height, base);
    render_window(bg);

    // Draw bar + button once.
    draw_rect(bar->fb, 0, 0, (int) bar->width, (int) bar->height, bar_col);
    draw_rect(bar->fb, btn_x, btn_y, btn_w, btn_h, btn_bg);
    draw_border(bar->fb, btn_x, btn_y, btn_w, btn_h, btn_border);
    draw_string(bar->fb, "Terminal", btn_x + 10, btn_y + 6, btn_text);
    render_window(bar);

    // Keep running as a background service.
    for (;;) {
        wm_event_t ev = get_event(bar);
        if (ev.type == 0) {
            sleep(50);
            continue;
        }

        if (ev.type == WM_EVENT_MOUSE_PRESS && ev.mouse.left_button) {
            int mx = ev.mouse.position.left;
            int my = ev.mouse.position.top;

            if (mx >= btn_x && mx < (btn_x + btn_w) && my >= btn_y && my < (btn_y + btn_h)) {
                char* exec_argv[] = {(char*) "terminal", NULL};
                (void) syscall2(SYS_EXEC, (uint32_t) "/bin/terminal", (uint32_t) exec_argv);
            }
        }
    }
}

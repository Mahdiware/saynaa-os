#include "libc/stdio.h"
#include "libc/string.h"
#include "libgui/gui.h"

static void draw(window_t* win, const char* status, const char* line) {
    if (!win) {
        return;
    }

    draw_window(win);
    draw_string(win->fb, (char*) status, 8, WM_TB_HEIGHT + 8, 0xFFE0E0E0);
    draw_string(win->fb, (char*) line, 8, WM_TB_HEIGHT + 28, 0xFFE0E0E0);
    render_window(win);
}

int main(int argc, char** argv) {
    (void) argc;
    (void) argv;

    window_t* win = open_window("kbdview", 420, 120, WM_NORMAL);
    if (!win) {
        return 1;
    }

    bool running = true;
    bool focused = true;

    char status[64];
    char line[96];
    strcpy(line, "press keys...");

    for (;;) {
        wm_event_t ev = get_event(win);

        if (ev.type == WM_EVENT_CLOSE) {
            running = false;
        } else if (ev.type == WM_EVENT_GAINED_FOCUS) {
            focused = true;
        } else if (ev.type == WM_EVENT_LOST_FOCUS) {
            focused = false;
        } else if (ev.type == WM_EVENT_KBD && ev.kbd.pressed) {
            char c = ev.kbd.repr;
            if (c == '\n') {
                snprintf(line, sizeof(line), "KBD down: keycode=%u repr=\\n", (unsigned) ev.kbd.keycode);
            } else if (c == '\b') {
                snprintf(line, sizeof(line), "KBD down: keycode=%u repr=\\b", (unsigned) ev.kbd.keycode);
            } else if (c == 0) {
                snprintf(line, sizeof(line), "KBD down: keycode=%u repr=0", (unsigned) ev.kbd.keycode);
            } else {
                snprintf(line, sizeof(line), "KBD down: keycode=%u repr='%c'", (unsigned) ev.kbd.keycode, c);
            }
        }

        snprintf(status, sizeof(status), "focus=%s", focused ? "yes" : "no");
        draw(win, status, line);

        if (!running) {
            break;
        }

        // Small sleep to avoid busy-loop.
        sleep(20);
    }

    close_window(win);
    return 0;
}

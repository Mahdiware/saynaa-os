#include "libc/stdio.h"
#include "libc/stdlib.h"
#include "libc/string.h"
#include "libc/syscall.h"
#include "libc/termios.h"
#include "libgui/gui.h"

static const uint32_t twidth = 550;
static const uint32_t theight = 342;
static const uint32_t char_width = 8;
static const uint32_t char_height = 16;
static const uint32_t margin = 8;
static const uint32_t text_color = 0xFFE0E0E0;
static const uint32_t text_bg_color = 0x1E1E2E;

static const uint32_t tick_ms = 20;

typedef struct {
    char* buf;
    uint32_t buf_len;
    uint32_t len;
} str_t;

static void reserve_stdio_fds(void) {
    int fd = sys_open("/dev/null", O_RDWR);
    if (fd > 2) {
        sys_close(fd);
    }
}

static str_t* str_new(const char* str) {
    str_t* s = (str_t*) malloc(sizeof(str_t));
    if (!s) {
        return NULL;
    }
    uint32_t size = (uint32_t) strlen(str) + 1;
    s->buf = (char*) malloc(size);
    if (!s->buf) {
        free(s);
        return NULL;
    }
    strcpy(s->buf, str);
    s->buf_len = size;
    s->len = size - 1;
    return s;
}

static void str_free(str_t* str) {
    if (!str) {
        return;
    }
    free(str->buf);
    free(str);
}

static void str_pop_last(str_t* str) {
    if (!str || str->len == 0) {
        return;
    }
    str->len--;
    str->buf[str->len] = '\0';
}

static bool str_append(str_t* str, const char* text) {
    if (!str || !text) {
        return false;
    }

    uint32_t prev_len = str->len;
    uint32_t needed = (uint32_t) strlen(text) + prev_len + 1;

    if (needed > str->buf_len) {
        uint32_t new_cap = str->buf_len ? str->buf_len : 128;
        while (new_cap < needed) {
            new_cap *= 2;
        }
        char* new_buf = (char*) realloc(str->buf, new_cap);
        if (!new_buf) {
            return false;
        }
        str->buf = new_buf;
        str->buf_len = new_cap;
    }

    strcpy(&str->buf[prev_len], text);
    str->len = needed - 1;
    return true;
}

static const char* strchrnul_local(const char* s, int c) {
    const char* p = s;
    while (*p && *p != (char) c) {
        p++;
    }
    return p;
}

static uint32_t count_lines(const str_t* str, uint32_t max_col) {
    const char* text_view = str->buf;
    uint32_t n_lines = 0;

    while (text_view < &str->buf[str->len]) {
        const char* lf = strchrnul_local(text_view, '\n');
        uint32_t line_len = (uint32_t) (lf - text_view);
        if (line_len <= max_col) {
            text_view = (*lf == '\n') ? (lf + 1) : lf;
        } else {
            text_view += max_col;
        }
        n_lines++;
    }

    return n_lines;
}

static const char* scroll_view(const char* s, uint32_t max_col) {
    const char* lf = strchrnul_local(s, '\n');
    uint32_t line_len = (uint32_t) (lf - s);
    if (line_len <= max_col) {
        return (*lf == '\n') ? (lf + 1) : lf;
    }
    return s + max_col;
}

static bool append_output(str_t* text_buf, const char* data, uint32_t len) {
    if (!text_buf || !data || len == 0) {
        return false;
    }

    bool changed = false;
    for (uint32_t i = 0; i < len; i++) {
        char c = data[i];

        if (c == '\f') {
            text_buf->len = 0;
            if (text_buf->buf_len > 0) {
                text_buf->buf[0] = '\0';
            }
            changed = true;
            continue;
        }

        if (c == '\r') {
            if (i + 1 < len && data[i + 1] == '\n') {
                continue;
            }
            c = '\n';
        }

        if (c == '\b') {
            str_pop_last(text_buf);
            changed = true;
            continue;
        }

        if (c == '\t') {
            for (int s = 0; s < 4; s++) {
                if (str_append(text_buf, " ")) {
                    changed = true;
                }
            }
            continue;
        }

        if (c < ' ' && c != '\n') {
            continue;
        }

        char tmp[2] = {c, '\0'};
        if (str_append(text_buf, tmp)) {
            changed = true;
        }
    }

    return changed;
}

static void redraw(window_t* win, str_t* text_buf) {
    draw_window(win);
    draw_rect(win->fb, (int) margin, (int) (WM_TB_HEIGHT + 4), (int) (twidth - 2 * margin),
        (int) (theight - (WM_TB_HEIGHT + 8)), text_bg_color);

    uint32_t max_col = twidth / char_width - 1;
    uint32_t max_line = theight / char_height - 2;
    uint32_t y = WM_TB_HEIGHT + 4;

    const char* text_view = text_buf->buf;
    uint32_t n_lines = count_lines(text_buf, max_col);
    if (n_lines > max_line) {
        for (uint32_t i = 0; i < n_lines - max_line; i++) {
            text_view = scroll_view(text_view, max_col);
        }
    }

    char line_buf[128];
    while (text_view < &text_buf->buf[text_buf->len]) {
        const char* lf = strchrnul_local(text_view, '\n');
        uint32_t line_len = (uint32_t) (lf - text_view);
        uint32_t copy = line_len <= max_col ? line_len : max_col;
        if (copy >= sizeof(line_buf)) {
            copy = sizeof(line_buf) - 1;
        }
        strncpy(line_buf, text_view, copy);
        line_buf[copy] = '\0';

        draw_string(win->fb, line_buf, (int) margin, (int) y, text_color);

        if (line_len <= max_col) {
            text_view = (*lf == '\n') ? (lf + 1) : lf;
        } else {
            text_view += max_col;
        }

        y += char_height;
        if (y >= theight) {
            break;
        }
    }

    render_window(win);
}

static bool setup_pty(int* master_fd, char* slave_path, size_t path_len) {
    int master = sys_open("/dev/ptmx", O_RDWR);
    if (master < 0) {
        return false;
    }

    uint32_t id = 0;
    if (sys_ioctl(master, TIOCGPTN, &id) < 0) {
        sys_close(master);
        return false;
    }

    snprintf(slave_path, path_len, "/dev/pts/%u", id);
    *master_fd = master;
    return true;
}

static int spawn_shell(const char* slave_path) {
    sys_close(0);
    sys_close(1);
    sys_close(2);

    int fd0 = sys_open(slave_path, O_RDWR);
    int fd1 = sys_open(slave_path, O_RDWR);
    int fd2 = sys_open(slave_path, O_RDWR);

    if (fd0 != 0 || fd1 != 1 || fd2 != 2) {
        return -1;
    }

    char* argv[] = {(char*) "shell", NULL};
    return (int) syscall2(SYS_EXEC, (uint32_t) "/bin/shell", (uint32_t) argv);
}

static char translate_key(const wm_kbd_event_t* kbd) {
    if (!kbd || !kbd->pressed) {
        return 0;
    }
    if (kbd->repr) {
        return kbd->repr;
    }
    if (kbd->keycode == 28) {
        return '\n';
    }
    if (kbd->keycode == 14) {
        return '\b';
    }
    return 0;
}

int main(int argc, char** argv) {
    (void) argc;
    (void) argv;

    reserve_stdio_fds();

    window_t* win = open_window("Terminal", (int) twidth, (int) theight, WM_NORMAL);
    if (!win) {
        sys_write(1, "terminal: window manager is not running\n", 43);
        return 1;
    }

    int master_fd = -1;
    char slave_path[32];
    if (!setup_pty(&master_fd, slave_path, sizeof(slave_path))) {
        sys_write(1, "terminal: failed to open ptmx\n", 33);
        close_window(win);
        return 1;
    }

    if (spawn_shell(slave_path) < 0) {
        sys_write(1, "terminal: failed to spawn shell\n", 34);
        close_window(win);
        return 1;
    }

    str_t* text_buf = str_new("");
    if (!text_buf) {
        close_window(win);
        return 1;
    }

    bool running = true;
    redraw(win, text_buf);

    while (running) {
        char out[256];
        int out_n = sys_read(master_fd, out, sizeof(out));
        bool needs_redraw = false;
        if (out_n > 0) {
            if (append_output(text_buf, out, (uint32_t) out_n)) {
                needs_redraw = true;
            }
        }

        for (;;) {
            wm_event_t ev = get_event(win);
            if (ev.type == 0) {
                break;
            }
            if (ev.type == WM_EVENT_CLOSE) {
                running = false;
                break;
            }
            if (ev.type == WM_EVENT_KBD && ev.kbd.pressed) {
                char ch = translate_key(&ev.kbd);
                if (ch) {
                    sys_write(master_fd, &ch, 1);
                }
            }
        }

        if (needs_redraw) {
            redraw(win, text_buf);
        }

        sleep(tick_ms);
    }

    str_free(text_buf);
    close_window(win);
    return 0;
}

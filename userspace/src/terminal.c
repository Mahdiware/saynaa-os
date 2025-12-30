#include "libc/ctype.h"
#include "libc/pathutil.h"
#include "libc/stdio.h"
#include "libc/stdlib.h"
#include "libc/string.h"
#include "libc/syscall.h"
#include "libgui/gui.h"

static const char* strchrnul_local(const char* s, int c) {
    const char* p = s;
    while (*p && *p != (char) c) {
        p++;
    }
    return p;
}

static int isspace_local(char c) {
    return (c == ' ') || (c == '\t') || (c == '\n') || (c == '\r') || (c == '\f') || (c == '\v');
}

static char* strndup_local(const char* s, uint32_t n) {
    char* out = (char*) malloc(n + 1);
    if (!out) {
        return NULL;
    }
    for (uint32_t i = 0; i < n; i++) {
        out[i] = s[i];
    }
    out[n] = '\0';
    return out;
}

typedef struct {
    char* buf;
    uint32_t buf_len;
    uint32_t len;
} str_t;

static str_t* str_new(const char* str);
static void str_free(str_t* str);
static bool str_append(str_t* str, const char* text);
static uint32_t count_lines(const str_t* str, uint32_t max_col);
static const char* scroll_view(const char* s, uint32_t max_col);
static void interpret_cmd(str_t* text_buf, str_t* input_buf, const char* cwd);
static void redraw(window_t* win, str_t* text_buf, const str_t* input_buf, bool cursor_on);
static void redraw_last_line(window_t* win, str_t* text_buf, const str_t* input_buf, bool cursor_on);

static const uint32_t twidth = 550;
static const uint32_t theight = 342;
static const uint32_t char_width = 8;
static const uint32_t char_height = 16;

static const char* prompt = "(shell) # ";
static const uint32_t margin = 8;
static const uint32_t text_color = 0xFFE0E0E0;
static const uint32_t text_bg_color = 0x1E1E2E;

static const uint32_t tick_ms = 20;
static const uint32_t blink_period_ms = 500;

int main(int argc, char** argv) {
    (void) argc;
    (void) argv;

    window_t* win = open_window("Terminal", (int) twidth, (int) theight, WM_NORMAL);
    if (!win) {
        sys_write(1, "terminal: window manager is not running\n", 43);
        return 1;
    }

    // Enable stdout capture for this process and its children.
    syscall0(SYS_MAKETTY);

    str_t* text_buf = str_new(prompt);
    str_t* input_buf = str_new("");
    if (!text_buf || !input_buf) {
        str_free(text_buf);
        str_free(input_buf);
        close_window(win);
        return 1;
    }

    bool running = true;
    bool focused = true;
    bool cursor_on = true;
    uint32_t blink_acc_ms = 0;

    char cwd[256];
    if (syscall2(SYS_GETCWD, (uint32_t) cwd, sizeof(cwd)) < 0) {
        strcpy(cwd, "/");
    }

    redraw(win, text_buf, input_buf, cursor_on);

    while (running) {
        // Read any output captured from executed programs.
        char out[256];
        int out_n = syscall2(SYS_READSTDOUT, (uint32_t) out, sizeof(out) - 1);
        bool needs_redraw = false;
        bool input_only = false;
        if (out_n > 0) {
            out[out_n] = '\0';
            if (str_append(text_buf, out)) {
                needs_redraw = true;
            }
        }

        // Drain all pending WM events (prevents event backlog from starving KBD).
        bool saw_event = false;
        for (;;) {
            wm_event_t ev = get_event(win);
            if (ev.type == 0) {
                break;
            }
            saw_event = true;

            if (ev.type == WM_EVENT_CLOSE) {
                running = false;
                break;
            }
            if (ev.type == WM_EVENT_GAINED_FOCUS) {
                focused = true;
                cursor_on = true;
                needs_redraw = true;
            } else if (ev.type == WM_EVENT_LOST_FOCUS) {
                focused = false;
                cursor_on = false;
                needs_redraw = true;
            }

            if (ev.type == WM_EVENT_KBD && ev.kbd.pressed) {
                char c = ev.kbd.repr;
                needs_redraw = true;
                input_only = true;

                if (c == '\n') {
                    input_only = false;
                    (void) str_append(text_buf, input_buf->buf);
                    if (strcmp(input_buf->buf, "exit") == 0) {
                        running = false;
                        break;
                    } else {
                        interpret_cmd(text_buf, input_buf, cwd);
                    }
                    (void) str_append(text_buf, "\n");
                    input_buf->buf[0] = '\0';
                    input_buf->len = 0;
                    if (running) {
                        (void) str_append(text_buf, prompt);
                    }
                } else if (c == '\b') {
                    if (input_buf->len) {
                        input_buf->buf[input_buf->len - 1] = '\0';
                        input_buf->len -= 1;
                    }
                } else if (c == 0) {
                    // Non-printable key with no ASCII representation.
                    needs_redraw = false;
                } else if ((unsigned char) c >= ' ' && (unsigned char) c < 127) {
                    char s[2] = {c, '\0'};
                    if (!str_append(input_buf, s)) {
                        needs_redraw = false;
                    }
                } else {
                    needs_redraw = false;
                }
            }
        }

        // Cursor blink when focused.
        if (focused) {
            blink_acc_ms += tick_ms;
            if (blink_acc_ms >= blink_period_ms) {
                blink_acc_ms = 0;
                cursor_on = !cursor_on;
                needs_redraw = true;
                input_only = true;
            }
        }

        if (needs_redraw) {
            uint32_t max_col = twidth / char_width - 1;

            // If the input line can wrap, fall back to full redraw.
            if (input_only && input_buf->len < max_col - 2) {
                redraw_last_line(win, text_buf, input_buf, cursor_on);
            } else {
                redraw(win, text_buf, input_buf, cursor_on);
            }
        }

        // Idle: don't busy-loop. Only tick fast when necessary.
        uint32_t sleep_ms = tick_ms;
        if (!saw_event && out_n <= 0 && !needs_redraw) {
            if (!focused) {
                sleep_ms = 50;
            } else {
                // Sleep until next blink, but cap to keep it responsive.
                uint32_t remain = (blink_acc_ms < blink_period_ms) ? (blink_period_ms - blink_acc_ms) : tick_ms;
                sleep_ms = remain;
                if (sleep_ms > 50) {
                    sleep_ms = 50;
                }
                if (sleep_ms < tick_ms) {
                    sleep_ms = tick_ms;
                }
            }
        }
        sleep(sleep_ms);
    }

    str_free(text_buf);
    str_free(input_buf);
    close_window(win);
    return 0;
}

static void redraw(window_t* win, str_t* text_buf, const str_t* input_buf, bool cursor_on) {
    draw_window(win);

    uint32_t max_col = twidth / char_width - 1;
    uint32_t max_line = theight / char_height - 2;

    uint32_t y = WM_TB_HEIGHT + 4;

    // Temporarily append input and cursor.
    if (!str_append(text_buf, input_buf->buf)) {
        return;
    }
    if (cursor_on) {
        if (!str_append(text_buf, "_")) {
            return;
        }
    }

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

        if (line_len <= max_col) {
            uint32_t copy = line_len;
            if (copy >= sizeof(line_buf)) {
                copy = sizeof(line_buf) - 1;
            }
            strncpy(line_buf, text_view, copy);
            line_buf[copy] = '\0';
            text_view = (*lf == '\n') ? (lf + 1) : lf;
        } else {
            uint32_t copy = max_col;
            if (copy >= sizeof(line_buf)) {
                copy = sizeof(line_buf) - 1;
            }
            strncpy(line_buf, text_view, copy);
            line_buf[copy] = '\0';
            text_view += max_col;
        }

        draw_string(win->fb, line_buf, (int) margin, (int) y, text_color);
        y += char_height;
        if (y >= theight) {
            break;
        }
    }

    // De-append input
    if (input_buf->len) {
        text_buf->buf[text_buf->len - input_buf->len] = '\0';
        text_buf->len -= input_buf->len;
    }

    if (cursor_on && text_buf->len) {
        text_buf->buf[text_buf->len - 1] = '\0';
        text_buf->len -= 1;
    }

    render_window(win);
}

static void redraw_last_line(window_t* win, str_t* text_buf, const str_t* input_buf, bool cursor_on) {
    uint32_t max_col = twidth / char_width - 1;
    uint32_t max_line = theight / char_height - 2;

    uint32_t y0 = WM_TB_HEIGHT + 4;
    uint32_t y = y0;

    // Temporarily append input and cursor.
    if (!str_append(text_buf, input_buf->buf)) {
        return;
    }
    if (cursor_on) {
        if (!str_append(text_buf, "_")) {
            return;
        }
    }

    const char* text_view = text_buf->buf;
    uint32_t n_lines = count_lines(text_buf, max_col);
    if (n_lines > max_line) {
        for (uint32_t i = 0; i < n_lines - max_line; i++) {
            text_view = scroll_view(text_view, max_col);
        }
    }

    char line_buf[128];
    char last_line[128];
    last_line[0] = '\0';
    uint32_t last_y = y;

    while (text_view < &text_buf->buf[text_buf->len]) {
        const char* lf = strchrnul_local(text_view, '\n');
        uint32_t line_len = (uint32_t) (lf - text_view);

        if (line_len <= max_col) {
            uint32_t copy = line_len;
            if (copy >= sizeof(line_buf)) {
                copy = sizeof(line_buf) - 1;
            }
            strncpy(line_buf, text_view, copy);
            line_buf[copy] = '\0';
            text_view = (*lf == '\n') ? (lf + 1) : lf;
        } else {
            uint32_t copy = max_col;
            if (copy >= sizeof(line_buf)) {
                copy = sizeof(line_buf) - 1;
            }
            strncpy(line_buf, text_view, copy);
            line_buf[copy] = '\0';
            text_view += max_col;
        }

        strncpy(last_line, line_buf, sizeof(last_line) - 1);
        last_line[sizeof(last_line) - 1] = '\0';
        last_y = y;

        y += char_height;
        if (y >= theight) {
            break;
        }
    }

    // Clear and redraw only the last visible line.
    draw_rect(win->fb, (int) margin, (int) last_y, (int) (twidth - 2 * margin), (int) char_height, text_bg_color);
    draw_string(win->fb, last_line, (int) margin, (int) last_y, text_color);

    // De-append input
    if (input_buf->len) {
        text_buf->buf[text_buf->len - input_buf->len] = '\0';
        text_buf->len -= input_buf->len;
    }

    if (cursor_on && text_buf->len) {
        text_buf->buf[text_buf->len - 1] = '\0';
        text_buf->len -= 1;
    }

    wm_rect_t clip = {
        .top = (int32_t) last_y,
        .left = 0,
        .bottom = (int32_t) (last_y + char_height - 1),
        .right = (int32_t) (twidth - 1),
    };
    render_window_partial(win, clip);
}

static str_t* str_new(const char* str) {
    str_t* s = (str_t*) malloc(sizeof(str_t));
    uint32_t size = (uint32_t) strlen(str) + 1;

    if (!s) {
        return NULL;
    }

    s->buf = (char*) malloc(size);
    s->buf_len = size;
    s->len = size - 1;

    if (!s->buf) {
        free(s);
        return NULL;
    }

    strcpy(s->buf, str);
    return s;
}

static void str_free(str_t* str) {
    if (!str) {
        return;
    }
    free(str->buf);
    free(str);
}

static bool str_append(str_t* str, const char* text) {
    if (!str || !text) {
        return false;
    }

    uint32_t prev_len = str->len;
    uint32_t needed = (uint32_t) strlen(text) + prev_len + 1;

    if (needed > str->buf_len) {
        uint32_t new_cap = str->buf_len ? str->buf_len : 16;
        while (new_cap < needed) {
            new_cap *= 2;
        }
        // grow a bit more to reduce realloc churn
        if (new_cap < 2 * needed) {
            new_cap = 2 * needed;
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

        n_lines += 1;
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

static void interpret_cmd(str_t* text_buf, str_t* input_buf, const char* cwd) {
    if (!strcmp(input_buf->buf, "exit")) {
        // handled by caller loop via buffer content; keep simple
        return;
    }

    char* cmd = input_buf->buf;
    uint32_t n_args = 0;
    char** args = NULL;
    char* next = cmd;

    while (*next) {
        while (isspace_local(*next)) {
            next++;
        }
        if (!*next) {
            break;
        }

        char** new_args = (char**) realloc(args, (n_args + 2) * sizeof(char*));
        if (!new_args) {
            (void) str_append(text_buf, "terminal: out of memory\n");
            goto cleanup;
        }
        args = new_args;
        n_args++;
        uint32_t n = (uint32_t) (strchrnul_local(next, ' ') - next);
        args[n_args - 1] = strndup_local(next, n);
        if (!args[n_args - 1]) {
            (void) str_append(text_buf, "terminal: out of memory\n");
            goto cleanup;
        }
        args[n_args] = NULL;
        next = (char*) strchrnul_local(next, ' ');
    }

    if (!args || !args[0]) {
        free(args);
        return;
    }

    // Built-in: clear the terminal
    if (!strcmp(args[0], "clear")) {
        text_buf->len = 0;
        if (text_buf->buf) {
            text_buf->buf[0] = '\0';
        }
        goto cleanup;
    }

    char path[256];
    if (strchr(args[0], '/')) {
        if (!make_abs_path(cwd, args[0], path, sizeof(path))) {
            (void) str_append(text_buf, "bad path\n");
            goto cleanup;
        }
    } else {
        snprintf(path, sizeof(path), "/bin/%s", args[0]);
    }

    int pid = (int) syscall2(SYS_EXEC, (uint32_t) path, (uint32_t) args);
    if (pid >= 0) {
        (void) syscall1(SYS_WAITPID, pid);
    } else {
        (void) str_append(text_buf, "\n");
        (void) str_append(text_buf, "unknown: ");
        (void) str_append(text_buf, args[0]);
        (void) str_append(text_buf, "\n");
    }

cleanup:
    if (args) {
        for (uint32_t i = 0; i < n_args; i++) {
            free(args[i]);
        }
        free(args);
    }
}

#include "libc/args.h"
#include "libc/pathutil.h"
#include "libc/stdio.h"
#include "libc/string.h"
#include "libc/syscall.h"

enum {
    SHELL_MAX_LINE = 256,
    SHELL_MAX_ARGS = 8,
};

static void shell_loop(void);
static void shell_prompt(const char* user, const char* host, const char* cwd, int last_status);
static void shell_read_line(char* buf, size_t max);
static int shell_run_builtin(const char* cwd, int argc, char* argv[]);
static int shell_exec_external(const char* cwd, int argc, char* argv[]);
static int read_tty_device_char(void);

int main(void) {
    shell_loop();
    return 0;
}

static void shell_loop(void) {
    char line[SHELL_MAX_LINE];
    char* argv[SHELL_MAX_ARGS + 1];
    const char* user = "user";
    const char* host = "saynaa";
    int last_status = 0;

    puts("User shell ready.");

    while (1) {
        char cwd[256];
        int cwd_result = sys_getcwd(cwd, sizeof(cwd));
        if (cwd_result < 0) {
            strcpy(cwd, "?");
        }

        shell_prompt(user, host, cwd, last_status);
        shell_read_line(line, sizeof(line));

        int argc = split_args(line, argv, SHELL_MAX_ARGS);
        if (argc == 0) {
            continue;
        }

        int builtin_status = shell_run_builtin(cwd, argc, argv);
        if (builtin_status >= 0) {
            last_status = builtin_status;
            continue;
        }

        last_status = shell_exec_external(cwd, argc, argv);
    }
}

static void shell_prompt(const char* user, const char* host, const char* cwd, int last_status) {
    printf("[%d] %s@%s:%s$ ", last_status, user, host, cwd);
}

static void shell_read_line(char* buf, size_t max) {
    size_t len = 0;
    while (1) {
        int c = read_tty_device_char();
        if (c == '\n') {
            sys_putchar('\n');
            buf[len] = '\0';
            return;
        } else if (c == '\b' || c == 127) {
            if (len > 0) {
                len--;
                sys_write("\b \b", 3);
            }
        } else if (c >= ' ' && c < 127) {
            if (len + 1 < max) {
                buf[len++] = (char) c;
                sys_putchar((char) c);
            }
        }
    }
}

static int shell_run_builtin(const char* cwd, int argc, char* argv[]) {
    (void) cwd;
    if (argc <= 0) {
        return 0;
    }

    if (strcmp(argv[0], "exit") == 0) {
        sys_exit();
        return 0;
    }

    if (strcmp(argv[0], "cd") == 0) {
        const char* target = (argc >= 2) ? argv[1] : "/";
        char abs[256];

        if (!make_abs_path(cwd, target, abs, sizeof(abs))) {
            puts("cd: bad path");
            return 1;
        }

        if (sys_chdir(abs) != 0) {
            puts("cd: failed");
            return 1;
        }

        return 0;
    }

    return -1;
}

static int shell_exec_external(const char* cwd, int argc, char* argv[]) {
    if (argc == 0) {
        return 1;
    }

    const char* cmd = argv[0];
    char path[256];

    if (strchr(cmd, '/')) {
        if (!make_abs_path(cwd, cmd, path, sizeof(path))) {
            printf("bad path: %s\n", cmd);
            return 1;
        }
    } else {
        snprintf(path, sizeof(path), "/bin/%s", cmd);
    }

    char* exec_argv[SHELL_MAX_ARGS + 1];
    int count = argc > SHELL_MAX_ARGS ? SHELL_MAX_ARGS : argc;

    for (int i = 0; i < count; i++) {
        exec_argv[i] = argv[i];
    }
    exec_argv[count] = NULL;

    int pid = sys_exec(path, exec_argv);
    if (pid < 0) {
        printf("unknown: %s\n", cmd);
        return 1;
    }
    if (sys_waitpid(pid) < 0) {
        return 1;
    }
    return 0;
}

static int read_tty_device_char(void) {
    char ch = 0;
    while (1) {
        int ret = sys_readfile("/dev/tty", 0, 1, &ch);
        if (ret == 1) {
            return (unsigned char) ch;
        }
    }
}

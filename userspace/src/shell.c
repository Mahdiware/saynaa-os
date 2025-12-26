#include "libc/pathutil.h"
#include "libc/stdio.h"
#include "libc/string.h"
#include "libc/syscall.h"

static void shell(void);
static int parse_args(char* line, char* argv[], int max);
static void read_line(char* buf, size_t max);
static int exec_command(const char* cwd, int argc, char* argv[]);

int main(void) {
    shell();
    return 0;
}

static void shell(void) {
    char line[256];
    char* argv[9];
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

        printf("[%d] %s@%s:%s$ ", last_status, user, host, cwd);
        read_line(line, sizeof(line));
        int argc = parse_args(line, argv, 8);
        if (argc == 0) {
            continue;
        }

        if (strcmp(argv[0], "exit") == 0) {
            sys_exit();
        }

        last_status = exec_command(cwd, argc, argv);
    }
}

static void read_line(char* buf, size_t max) {
    size_t len = 0;
    while (1) {
        int c = sys_getchar();
        if (c < 0) {
            continue;
        }
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

static int parse_args(char* line, char* argv[], int max) {
    int argc = 0;
    char* p = line;
    while (*p && argc < max) {
        while (*p == ' ') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        argv[argc++] = p;
        while (*p && *p != ' ') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        *p++ = '\0';
    }
    argv[argc] = NULL;
    return argc;
}

static int exec_command(const char* cwd, int argc, char* argv[]) {
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

    char resolved[8][256];
    char* exec_argv[9];
    int count = argc > 8 ? 8 : argc;

    for (int i = 0; i < count; i++) {
        if (i > 0 && (strcmp(cmd, "ls") == 0 || strcmp(cmd, "cat") == 0)) {
            if (!make_abs_path(cwd, argv[i], resolved[i], sizeof(resolved[i]))) {
                printf("bad path: %s\n", argv[i]);
                return 1;
            }
            exec_argv[i] = resolved[i];
        } else {
            exec_argv[i] = argv[i];
        }
    }
    exec_argv[count] = NULL;

    int pid = sys_exec(path, exec_argv);
    if (pid < 0) {
        printf("unknown: %s\n", cmd);
        return 1;
    }
    sys_waitpid(pid);
    return 0;
}

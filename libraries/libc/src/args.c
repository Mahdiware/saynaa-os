#include "libc/args.h"

static int is_ws(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

int split_args(char* line, char* argv[], int max_args) {
    if (!line || !argv || max_args <= 0) {
        if (argv) {
            argv[0] = 0;
        }
        return 0;
    }

    int argc = 0;
    char* p = line;

    while (*p && argc < max_args) {
        while (*p && is_ws(*p)) {
            p++;
        }
        if (*p == '\0') {
            break;
        }

        argv[argc++] = p;

        while (*p && !is_ws(*p)) {
            p++;
        }

        if (*p == '\0') {
            break;
        }

        *p++ = '\0';
    }

    argv[argc] = 0;
    return argc;
}

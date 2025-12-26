#include "libc/pathutil.h"

#include "libc/stdio.h"
#include "libc/string.h"

int normalize_path(char* path) {
    if (!path || path[0] == '\0') {
        return 0;
    }
    char* segments[64];
    int top = 0;
    char temp[256];
    strncpy(temp, path, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';
    char* token = temp;
    while (*token) {
        while (*token == '/') {
            token++;
        }
        if (*token == '\0') {
            break;
        }
        char* start = token;
        while (*token && *token != '/') {
            token++;
        }
        if (*token) {
            *token++ = '\0';
        }
        if (strcmp(start, ".") == 0) {
            continue;
        }
        if (strcmp(start, "..") == 0) {
            if (top > 0) {
                top--;
            }
            continue;
        }
        if (top < 64) {
            segments[top++] = start;
        }
    }
    if (top == 0) {
        strcpy(path, "/");
        return 1;
    }
    path[0] = '\0';
    for (int i = 0; i < top; i++) {
        strcat(path, "/");
        strcat(path, segments[i]);
    }
    return 1;
}

int make_abs_path(const char* cwd, const char* input, char* out, size_t out_len) {
    if (!cwd || !input || !out || out_len == 0) {
        return 0;
    }
    if (input[0] == '/') {
        strncpy(out, input, out_len - 1);
        out[out_len - 1] = '\0';
    } else {
        if (strcmp(cwd, "/") == 0) {
            snprintf(out, out_len, "/%s", input);
        } else {
            snprintf(out, out_len, "%s/%s", cwd, input);
        }
    }
    return normalize_path(out);
}

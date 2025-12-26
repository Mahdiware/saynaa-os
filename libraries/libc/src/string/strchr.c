#include "libc/string.h"

char* strchr(const char* s, int c) {
    if (!s) {
        return 0;
    }
    while (*s) {
        if (*s == (char) c) {
            return (char*) s;
        }
        s++;
    }
    if ((char) c == '\0') {
        return (char*) s;
    }
    return 0;
}

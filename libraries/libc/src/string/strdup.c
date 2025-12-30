#include "libc/stdlib.h"
#include "libc/string.h"

char* strdup(const char* s) {
    if (!s) {
        return NULL;
    }

    size_t len = (size_t) strlen(s);
    char* out = (char*) malloc(len + 1);
    if (!out) {
        return NULL;
    }

    memcpy(out, s, (uint32_t) (len + 1));
    return out;
}

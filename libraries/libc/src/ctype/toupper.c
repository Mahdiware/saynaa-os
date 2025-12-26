#include "libc/ctype.h"

char toupper(char c) {
    if ((c >= 'a') && (c <= 'z'))
        return (c - 32);
    return c;
}
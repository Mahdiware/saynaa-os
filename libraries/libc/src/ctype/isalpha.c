#include "libc/ctype.h"

int isalpha(char c) {
    return (((c >= 'A') && (c <= 'Z')) || ((c >= 'a') && (c <= 'z')));
}
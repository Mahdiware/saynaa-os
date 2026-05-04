#include "libc/syscall.h"

int main(void) {
    (void) sys_write(1, "\f", 1);
    return 0;
}

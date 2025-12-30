#include "libc/syscall.h"

int main(void) {
    syscall1(SYS_PUTCHAR, '\f');
    return 0;
}

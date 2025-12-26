#include "libc/stdio.h"
#include "libc/syscall.h"

int main(void) {
    char cwd[256];
    if (sys_getcwd(cwd, sizeof(cwd)) < 0) {
        puts("pwd: failed");
        return 1;
    }

    puts(cwd);
    return 0;
}

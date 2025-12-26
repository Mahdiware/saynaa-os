#include "libc/stdio.h"
#include "libc/syscall.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        puts("cd: missing arg");
        return 1;
    }

    if (sys_chdir(argv[1]) != 0) {
        puts("cd: failed");
        return 1;
    }

    return 0;
}

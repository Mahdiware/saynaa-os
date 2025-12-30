#include "libc/string.h"
#include "libc/syscall.h"

int main(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        sys_write(1, (char*) argv[i], strlen(argv[i]));
        if (i + 1 < argc)
            sys_write(1, " ", 1);
    }
    sys_write(1, "\n", 1);
    return 0;
}

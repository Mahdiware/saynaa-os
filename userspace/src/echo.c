#include "libc/string.h"
#include "libc/syscall.h"

int main(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        sys_write(argv[i], strlen(argv[i]));
        if (i + 1 < argc)
            sys_write(" ", 1);
    }
    sys_write("\n", 1);
    return 0;
}

#include "libc/pathutil.h"
#include "libc/stdio.h"
#include "libc/string.h"
#include "libc/syscall.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        puts("cat: missing file");
        return 1;
    }

    const char* arg = argv[1];
    char path[256];
    if (!make_abs_path("/", arg, path, sizeof(path))) {
        puts("cat: bad path");
        return 1;
    }

    char buf[512];
    uint32_t offset = 0;
    int rc = 0;
    while (1) {
        int n = sys_readfile(path, offset, sizeof(buf) - 1, buf);
        if (n < 0) {
            rc = 1;
            break;
        }
        if (n == 0)
            break;
        buf[n] = '\0';
        sys_write(buf, (uint32_t) n);
        offset += (uint32_t) n;
        if ((uint32_t) n < sizeof(buf) - 1)
            break;
    }
    sys_write("\n", 1);
    return rc;
}

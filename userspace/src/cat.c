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

    int fd = sys_open(path, O_RDONLY);
    if (fd < 0) {
        puts("cat: open failed");
        return 1;
    }

    char buf[512];
    int rc = 0;
    while (1) {
        int n = sys_read(fd, buf, sizeof(buf));
        if (n < 0) {
            rc = 1;
            break;
        }
        if (n == 0) {
            break;
        }
        sys_write(1, buf, (uint32_t) n);
        if ((uint32_t) n < sizeof(buf)) {
            break;
        }
    }
    sys_write(1, "\n", 1);
    sys_close(fd);
    return rc;
}

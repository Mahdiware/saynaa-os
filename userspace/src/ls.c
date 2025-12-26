#include "libc/pathutil.h"
#include "libc/stdio.h"
#include "libc/string.h"
#include "libc/syscall.h"

int main(int argc, char** argv) {
    const char* arg = argc > 1 ? argv[1] : NULL;
    char cwd[256];
    strcpy(cwd, "/");

    char path[256];
    if (!arg || arg[0] == '\0') {
        strncpy(path, cwd, sizeof(path));
    } else if (!make_abs_path(cwd, arg, path, sizeof(path))) {
        puts("ls: bad path");
        return 1;
    }

    sys_dirent_t ent;
    uint32_t idx = 0;
    while (sys_readdir(path, idx++, &ent) == 0) {
        char full[256];
        make_abs_path(path, ent.name, full, sizeof(full));
        sys_stat_t st;
        int is_dir = (sys_stat(full, &st) == 0) && (st.flags & SYS_NODE_DIR);
        if (is_dir) {
            printf("%s/\n", ent.name);
        } else {
            printf("%s\n", ent.name);
        }
    }
    return 0;
}

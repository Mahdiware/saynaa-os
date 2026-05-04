#include "libc/stdio.h"
#include "libc/syscall.h"

int main(int argc, char** argv) {
    (void) argc;
    (void) argv;

    (void) syscall1(SYS_SET_KBD_MODE, 1);

    char* wm_argv[] = {(char*) "wm", NULL};
    int wm_pid = (int) syscall2(SYS_EXEC, (uint32_t) "/bin/wm", (uint32_t) wm_argv);
    if (wm_pid < 0) {
        puts("startwm: failed to start wm");
        (void) syscall1(SYS_SET_KBD_MODE, 0);
        return 1;
    }

    char* bg_argv[] = {(char*) "background", NULL};
    int bg_pid = (int) syscall2(SYS_EXEC, (uint32_t) "/bin/background", (uint32_t) bg_argv);
    if (bg_pid < 0) {
        puts("startwm: failed to start background");
    }

    (void) syscall1(SYS_WAITPID, (uint32_t) wm_pid);
    (void) syscall1(SYS_SET_KBD_MODE, 0);

    return 0;
}

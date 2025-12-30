#include "libc/string.h"
#include "libc/syscall.h"

int main(void) {
    const char* msg = "Hello from userspace via sys_write!\n";
    sys_write(1, (char*) msg, (uint32_t) strlen(msg));

    int pid = syscall0(SYS_GETPID) & 0xF;
    char c = (pid < 10) ? (char) ('0' + pid) : (char) ('a' + (pid - 10));
    syscall1(SYS_PUTCHAR, c);
    syscall1(SYS_PUTCHAR, '\n');

    syscall0(SYS_YIELD);
    syscall0(SYS_EXIT);
    return 0;
}

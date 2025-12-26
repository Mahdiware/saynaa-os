#include "libc/string.h"
#include "libc/syscall.h"

int main(void) {
    const char* msg = "Hello from userspace via sys_write!\n";
    sys_write(msg, (uint32_t) strlen(msg));

    int pid = sys_getpid() & 0xF;
    char c = (pid < 10) ? (char) ('0' + pid) : (char) ('a' + (pid - 10));
    sys_putchar(c);
    sys_putchar('\n');

    sys_yield();
    sys_exit();
    return 0;
}

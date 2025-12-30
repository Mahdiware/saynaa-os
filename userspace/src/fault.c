#include "libc/string.h"
#include "libc/syscall.h"

int main(void) {
    const char* msg = "fault: intentionally touching invalid memory...\n";
    sys_write(1, (const void*) msg, (uint32_t) strlen(msg));

    volatile uint32_t* bad = (uint32_t*) 0x1; // provoke a page fault
    *bad = 0xDEADBEEF;

    // Should never reach here
    sys_write(1, "fault: survived?\n", 18);
    return 0;
}
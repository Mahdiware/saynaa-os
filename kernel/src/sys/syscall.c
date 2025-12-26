#include "kernel/sys/syscall.h"

#include "kernel/cpu/isr.h"
#include "kernel/cpu/timer.h"
#include "kernel/drivers/keyboard.h"
#include "kernel/fs/vfs.h"
#include "kernel/kernel.h"
#include "kernel/lib/kprintf.h"
#include "kernel/sys/proc.h"
#include "libc/pathutil.h"
#include "libc/stdio.h"
#include "libc/stdlib.h"
#include "libc/string.h"

extern sched_t* scheduler;

static void syscall_handler(REGISTERS* regs);

extern process_t* current_process;

static void syscall_yield(REGISTERS* regs);
static void syscall_exit(REGISTERS* regs);
static void syscall_wait(REGISTERS* regs);
static void syscall_putchar(REGISTERS* regs);
static void syscall_write(REGISTERS* regs);
static void syscall_getpid(REGISTERS* regs);
static void syscall_readfile(REGISTERS* regs);
static void syscall_readdir(REGISTERS* regs);
static void syscall_exec(REGISTERS* regs);
static void syscall_stat(REGISTERS* regs);
static void syscall_getchar(REGISTERS* regs);
static void syscall_waitpid(REGISTERS* regs);
static void syscall_getcwd(REGISTERS* regs);
static void syscall_chdir(REGISTERS* regs);

sys_handler_t syscall_handlers[SYSCALL_NUM] = {0};

void init_syscall() {
    isr_register_handler(48, &syscall_handler);

    syscall_handlers[SYS_exit] = syscall_exit;
    syscall_handlers[SYS_putchar] = syscall_putchar;
    syscall_handlers[SYS_write] = syscall_write;
    syscall_handlers[SYS_getpid] = syscall_getpid;
    syscall_handlers[SYS_yield] = syscall_yield;
    syscall_handlers[SYS_readfile] = syscall_readfile;
    syscall_handlers[SYS_readdir] = syscall_readdir;
    syscall_handlers[SYS_exec] = syscall_exec;
    syscall_handlers[SYS_stat] = syscall_stat;
    syscall_handlers[SYS_getchar] = syscall_getchar;
    syscall_handlers[SYS_waitpid] = syscall_waitpid;
    syscall_handlers[SYS_getcwd] = syscall_getcwd;
    syscall_handlers[SYS_chdir] = syscall_chdir;
}

static void syscall_handler(REGISTERS* regs) {
    if (syscall_handlers[regs->eax]) {
        sys_handler_t handler = syscall_handlers[regs->eax];
        handler(regs);
    } else {
        kprintf("Unknown syscall %d\n", regs->eax);
    }
}

static void syscall_exit(REGISTERS* regs) {
    unused(regs);
    proc_exit();
}

static void syscall_putchar(REGISTERS* regs) {
    vbe_print_char((char) regs->ebx);
}

static void syscall_write(REGISTERS* regs) {
    const char* buf = (const char*) regs->ebx;
    uint32_t len = regs->ecx;
    if (!buf || len == 0) {
        regs->eax = -1;
        return;
    }
    for (uint32_t i = 0; i < len; i++) {
        vbe_print_char(buf[i]);
    }
    regs->eax = (int32_t) len;
}

static int resolve_user_path(const char* path, char* out, size_t out_len) {
    if (!path || !out || out_len == 0 || !current_process) {
        return 0;
    }
    return make_abs_path(current_process->cwd, path, out, out_len);
}

static void syscall_getpid(REGISTERS* regs) {
    regs->eax = (int32_t) proc_get_current_pid();
}

static void syscall_yield(REGISTERS* regs) {
    unused(regs);
    proc_schedule();
}

static void syscall_readfile(REGISTERS* regs) {
    const char* path = (const char*) regs->ebx;
    uint32_t offset = regs->ecx;
    uint32_t size = regs->edx;
    uint8_t* buf = (uint8_t*) regs->esi;
    char resolved[256];

    if (!path || !buf) {
        regs->eax = -1;
        return;
    }
    if (!resolve_user_path(path, resolved, sizeof(resolved))) {
        regs->eax = -1;
        return;
    }
    ssize_t r = vfs_pread(resolved, offset, size, buf);
    regs->eax = (int32_t) r;
}

static void syscall_readdir(REGISTERS* regs) {
    const char* path = (const char*) regs->ebx;
    uint32_t index = regs->ecx;
    vfs_dirent_t* out = (vfs_dirent_t*) regs->edx;
    char resolved[256];

    if (!path || !out) {
        regs->eax = -1;
        return;
    }
    if (!resolve_user_path(path, resolved, sizeof(resolved))) {
        regs->eax = -1;
        return;
    }
    vfs_node_t* node = vfs_lookup(resolved);
    if (!node) {
        regs->eax = -1;
        return;
    }
    regs->eax = vfs_readdir(node, index, out);
}

static void syscall_exec(REGISTERS* regs) {
    const char* path = (const char*) regs->ebx;
    char** argv = (char**) regs->ecx;
    char resolved[256];

    if (!path) {
        regs->eax = -1;
        return;
    }
    if (!resolve_user_path(path, resolved, sizeof(resolved))) {
        regs->eax = -1;
        return;
    }
    process_t* p = proc_run_path(resolved, argv);
    if (!p) {
        regs->eax = -1;
        return;
    }
    regs->eax = (int32_t) p->pid;
}

typedef struct syscall_stat {
    uint32_t flags;
    uint32_t size;
} syscall_stat_t;

static void syscall_stat(REGISTERS* regs) {
    const char* path = (const char*) regs->ebx;
    syscall_stat_t* st = (syscall_stat_t*) regs->ecx;
    char resolved[256];

    if (!path || !st) {
        regs->eax = -1;
        return;
    }
    if (!resolve_user_path(path, resolved, sizeof(resolved))) {
        regs->eax = -1;
        return;
    }
    vfs_node_t* node = vfs_lookup(resolved);
    if (!node) {
        regs->eax = -1;
        return;
    }
    st->flags = node->flags;
    st->size = node->length;
    regs->eax = 0;
}

static void syscall_getchar(REGISTERS* regs) {
    enable_interrupts();
    regs->eax = (int32_t) kb_getchar();
}

static void syscall_waitpid(REGISTERS* regs) {
    uint32_t pid = (uint32_t) regs->ebx;
    if (!scheduler || !scheduler->sched_exists) {
        regs->eax = -1;
        return;
    }

    while (scheduler->sched_exists(scheduler, pid)) {
        proc_schedule();
    }

    regs->eax = 0;
}

static void syscall_getcwd(REGISTERS* regs) {
    char* buf = (char*) regs->ebx;
    uint32_t len = regs->ecx;

    if (!buf || len == 0 || !current_process) {
        regs->eax = -1;
        return;
    }

    size_t cwd_len = strlen(current_process->cwd);
    if (cwd_len + 1 > len) {
        regs->eax = -1;
        return;
    }

    strncpy(buf, current_process->cwd, len - 1);
    buf[len - 1] = '\0';
    regs->eax = (int32_t) cwd_len;
}

static void syscall_chdir(REGISTERS* regs) {
    const char* path = (const char*) regs->ebx;
    char resolved[256];

    if (!resolve_user_path(path, resolved, sizeof(resolved))) {
        regs->eax = -1;
        return;
    }

    vfs_node_t* node = vfs_lookup(resolved);
    if (!node || !(node->flags & VFS_NODE_DIR)) {
        regs->eax = -1;
        return;
    }

    strncpy(current_process->cwd, resolved, sizeof(current_process->cwd) - 1);
    current_process->cwd[sizeof(current_process->cwd) - 1] = '\0';
    regs->eax = 0;
}

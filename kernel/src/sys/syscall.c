#include "kernel/sys/syscall.h"

#include "kernel/cpu/isr.h"
#include "kernel/cpu/timer.h"
#include "kernel/fs/dev/dev_tty.h"
#include "kernel/fs/vfs.h"
#include "kernel/kernel.h"
#include "kernel/lib/kprintf.h"
#include "kernel/mem/malloc.h"
#include "kernel/mem/shm.h"
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
static void syscall_read(REGISTERS* regs);
static void syscall_open(REGISTERS* regs);
static void syscall_close(REGISTERS* regs);
static void syscall_seek(REGISTERS* regs);
static void syscall_readdir(REGISTERS* regs);
static void syscall_exec(REGISTERS* regs);
static void syscall_stat(REGISTERS* regs);
static void syscall_getchar(REGISTERS* regs);
static void syscall_waitpid(REGISTERS* regs);
static void syscall_getcwd(REGISTERS* regs);
static void syscall_chdir(REGISTERS* regs);
static void syscall_sleep(REGISTERS* regs);
static void syscall_sbrk(REGISTERS* regs);
static void syscall_maketty(REGISTERS* regs);
static void syscall_readstdout(REGISTERS* regs);
static void syscall_shm_create(REGISTERS* regs);
static void syscall_shm_map(REGISTERS* regs);
static void syscall_shm_close(REGISTERS* regs);

static proc_fd_t* fd_from_index(int fd);
static int alloc_fd(vfs_node_t* node, uint32_t flags);

sys_handler_t syscall_handlers[SYSCALL_NUM] = {0};

void init_syscall() {
    isr_register_handler(48, &syscall_handler);

    syscall_handlers[SYS_EXIT] = syscall_exit;
    syscall_handlers[SYS_PUTCHAR] = syscall_putchar;
    syscall_handlers[SYS_WRITE] = syscall_write;
    syscall_handlers[SYS_READ] = syscall_read;
    syscall_handlers[SYS_OPEN] = syscall_open;
    syscall_handlers[SYS_CLOSE] = syscall_close;
    syscall_handlers[SYS_SEEK] = syscall_seek;
    syscall_handlers[SYS_GETPID] = syscall_getpid;
    syscall_handlers[SYS_YIELD] = syscall_yield;
    syscall_handlers[SYS_READDIR] = syscall_readdir;
    syscall_handlers[SYS_EXEC] = syscall_exec;
    syscall_handlers[SYS_STAT] = syscall_stat;
    syscall_handlers[SYS_GETCHAR] = syscall_getchar;
    syscall_handlers[SYS_WAITPID] = syscall_waitpid;
    syscall_handlers[SYS_GETCWD] = syscall_getcwd;
    syscall_handlers[SYS_CHDIR] = syscall_chdir;
    syscall_handlers[SYS_SLEEP] = syscall_sleep;
    syscall_handlers[SYS_SBRK] = syscall_sbrk;
    syscall_handlers[SYS_MAKETTY] = syscall_maketty;
    syscall_handlers[SYS_READSTDOUT] = syscall_readstdout;
    syscall_handlers[SYS_SHM_CREATE] = syscall_shm_create;
    syscall_handlers[SYS_SHM_MAP] = syscall_shm_map;
    syscall_handlers[SYS_SHM_CLOSE] = syscall_shm_close;
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
    uint8_t ch = (uint8_t) regs->ebx;

    if (current_process && current_process->stdout) {
        proc_stdout_t* out = current_process->stdout;
        uint32_t next = (out->w + 1) % (uint32_t) sizeof(out->buf);
        if (next == out->r) {
            out->r = (out->r + 1) % (uint32_t) sizeof(out->buf);
        }
        out->buf[out->w] = ch;
        out->w = next;
        regs->eax = 1;
        return;
    }

    ttydev_write_active(0, 1, &ch);
    regs->eax = 1;
}

static void syscall_write(REGISTERS* regs) {
    int fd = (int) regs->ebx;
    const uint8_t* buf = (const uint8_t*) regs->ecx;
    uint32_t len = regs->edx;

    if (!buf || len == 0 || fd < 0) {
        regs->eax = -1;
        return;
    }

    // Preserve captured stdout behaviour for GUI terminals
    if (fd == 1 && current_process && current_process->stdout) {
        proc_stdout_t* out = current_process->stdout;
        for (uint32_t i = 0; i < len; i++) {
            uint32_t next = (out->w + 1) % (uint32_t) sizeof(out->buf);
            if (next == out->r) {
                out->r = (out->r + 1) % (uint32_t) sizeof(out->buf);
            }
            out->buf[out->w] = buf[i];
            out->w = next;
        }
        regs->eax = (int32_t) len;
        return;
    }

    proc_fd_t* entry = fd_from_index(fd);
    if (!entry || !(entry->flags & O_WRONLY) || !entry->node) {
        // Fallback to the active TTY for legacy behaviour
        regs->eax = (int32_t) ttydev_write_active(0, len, buf);
        return;
    }

    ssize_t written = vfs_write(entry->node, entry->offset, len, buf);
    if (written < 0) {
        // Devices might not care about offsets; always fall back to console so user output is visible.
        regs->eax = (int32_t) ttydev_write_active(0, len, buf);
        return;
    }
    if (written > 0) {
        entry->offset += (uint32_t) written;
    }
    regs->eax = (int32_t) written;
}

static int resolve_user_path(const char* path, char* out, size_t out_len) {
    if (!path || !out || out_len == 0 || !current_process) {
        return 0;
    }
    return make_abs_path(current_process->cwd, path, out, out_len);
}

static proc_fd_t* fd_from_index(int fd) {
    if (!current_process || fd < 0 || fd >= (int) PROC_MAX_FD) {
        return NULL;
    }
    proc_fd_t* entry = &current_process->fds[fd];
    return entry->used ? entry : NULL;
}

static int alloc_fd(vfs_node_t* node, uint32_t flags) {
    if (!current_process || !node) {
        return -1;
    }
    for (int i = 0; i < (int) PROC_MAX_FD; i++) {
        if (!current_process->fds[i].used) {
            current_process->fds[i].used = true;
            current_process->fds[i].flags = flags;
            current_process->fds[i].node = node;
            current_process->fds[i].offset = (flags & O_APPEND) ? node->length : 0;
            return i;
        }
    }
    return -1;
}

static void syscall_getpid(REGISTERS* regs) {
    regs->eax = (int32_t) proc_get_current_pid();
}

static void syscall_yield(REGISTERS* regs) {
    unused(regs);
    proc_schedule();
}

static void syscall_open(REGISTERS* regs) {
    const char* path = (const char*) regs->ebx;
    uint32_t flags = regs->ecx;
    char resolved[256];

    if (!path) {
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

    if ((flags & O_RDONLY) && !node->ops.read) {
        regs->eax = -1;
        return;
    }
    if ((flags & O_WRONLY) && !node->ops.write) {
        regs->eax = -1;
        return;
    }

    int fd = alloc_fd(node, flags);
    regs->eax = (int32_t) fd;
}

static void syscall_close(REGISTERS* regs) {
    int fd = (int) regs->ebx;
    proc_fd_t* entry = fd_from_index(fd);
    if (!entry) {
        regs->eax = -1;
        return;
    }
    memset(entry, 0, sizeof(*entry));
    regs->eax = 0;
}

static void syscall_read(REGISTERS* regs) {
    int fd = (int) regs->ebx;
    uint8_t* buf = (uint8_t*) regs->ecx;
    uint32_t size = regs->edx;

    if (!buf || size == 0) {
        regs->eax = -1;
        return;
    }

    proc_fd_t* entry = fd_from_index(fd);
    if (!entry || !(entry->flags & O_RDONLY) || !entry->node) {
        // Legacy stdin behaviour: try active TTY
        regs->eax = (int32_t) ttydev_read_active(0, size, buf);
        return;
    }

    ssize_t r = vfs_read(entry->node, entry->offset, size, buf);
    if (r < 0) {
        regs->eax = (int32_t) ttydev_read_active(0, size, buf);
        return;
    }
    if (r > 0) {
        entry->offset += (uint32_t) r;
    }
    regs->eax = (int32_t) r;
}

static void syscall_seek(REGISTERS* regs) {
    int fd = (int) regs->ebx;
    int32_t offset = (int32_t) regs->ecx;
    uint32_t whence = regs->edx;

    proc_fd_t* entry = fd_from_index(fd);
    if (!entry || !entry->node) {
        regs->eax = -1;
        return;
    }

    int64_t base = 0;
    switch (whence) {
    case SYS_SEEK_SET:
        base = 0;
        break;
    case SYS_SEEK_CUR:
        base = entry->offset;
        break;
    case SYS_SEEK_END:
        base = entry->node->length;
        break;
    default:
        regs->eax = -1;
        return;
    }

    int64_t new_off = base + offset;
    if (new_off < 0) {
        regs->eax = -1;
        return;
    }

    entry->offset = (uint32_t) new_off;
    regs->eax = (int32_t) entry->offset;
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
    uint8_t ch = 0;
    ssize_t r = ttydev_read_active(0, 1, &ch);
    if (r == 1) {
        regs->eax = (int32_t) ch;
    } else {
        regs->eax = -1;
    }
}

static void syscall_shm_create(REGISTERS* regs) {
    uint32_t size = (uint32_t) regs->ebx;
    uint32_t id = 0;
    if (shm_create(size, &id) != 0) {
        regs->eax = -1;
        return;
    }
    regs->eax = (int32_t) id;
}

static void syscall_shm_map(REGISTERS* regs) {
    uint32_t id = (uint32_t) regs->ebx;
    void* addr = shm_map(id);
    regs->eax = (uintptr_t) addr;
}

static void syscall_shm_close(REGISTERS* regs) {
    uint32_t id = (uint32_t) regs->ebx;
    regs->eax = shm_close(id);
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

static void syscall_sleep(REGISTERS* regs) {
    uint32_t ms = (uint32_t) regs->ebx;
    proc_sleep(ms);
    regs->eax = 0;
}

static void syscall_sbrk(REGISTERS* regs) {
    intptr_t inc = (intptr_t) regs->ebx;
    regs->eax = (uintptr_t) proc_sbrk(inc);
}

static void syscall_maketty(REGISTERS* regs) {
    if (!current_process) {
        regs->eax = -1;
        return;
    }

    if (current_process->stdout) {
        regs->eax = 0;
        return;
    }

    proc_stdout_t* out = (proc_stdout_t*) kmalloc(sizeof(proc_stdout_t));
    if (!out) {
        regs->eax = -1;
        return;
    }
    memset(out, 0, sizeof(*out));
    out->refcount = 1;
    current_process->stdout = out;
    regs->eax = 0;
}

static void syscall_readstdout(REGISTERS* regs) {
    void* buf = (void*) regs->ebx;
    uint32_t size = (uint32_t) regs->ecx;
    if (!current_process || !current_process->stdout || !buf || size == 0) {
        regs->eax = 0;
        return;
    }

    proc_stdout_t* out = current_process->stdout;
    uint8_t* dst = (uint8_t*) buf;
    uint32_t read = 0;

    while (read < size && out->r != out->w) {
        dst[read++] = out->buf[out->r];
        out->r = (out->r + 1) % (uint32_t) sizeof(out->buf);
    }

    regs->eax = (int32_t) read;
}

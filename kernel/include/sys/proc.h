#pragma once

#include "kernel/fs/vfs.h"
#include "kernel/kernel.h"
#include "kernel/lib/kprintf.h"
#include "kernel/utils/debug.h"
#include "kernel/utils/linkedlist.h"
#include "libc/stddef.h"
#include "libc/stdint.h"
#include "libc/string.h"

#define PROC_STACK_PAGES 4
#define PROC_KERNEL_STACK_PAGES 1
#define PROC_MAX_FD 32

typedef struct proc_stdout {
    uint32_t refcount;
    uint32_t r;
    uint32_t w;
    uint8_t buf[4096];
} proc_stdout_t;

typedef struct proc_fd {
    bool used;
    uint32_t flags;
    uint32_t offset;
    vfs_node_t* node;
} proc_fd_t;

// Add new members to the end to avoid messing with the offsets
typedef struct _proc_t {
    uint32_t magic; // guard against corruption
    uint32_t pid;
    // Sizes of the exectuable and of the stack in number of pages
    uint32_t stack_len;
    uint32_t code_len;
    uintptr_t directory;
    // Kernel stack used when the process is preempted
    uintptr_t kernel_stack;
    // Kernel stack to restore when the process is switched to
    uintptr_t saved_kernel_stack;
    // Stack to use when first switching to userspace for a new process
    uintptr_t initial_user_stack;
    uint32_t mem_len; // Size of program heap in bytes
    uint32_t sleep_ticks;
    uint8_t fpu_registers[512];
    uint32_t parent_pid; // PID of creator (0 for kernel/root)
    char cwd[256];

    // Shared stdout capture buffer (used by GUI terminal).
    proc_stdout_t* stdout;

    // Next virtual address for shared-memory mappings (grows downward).
    uintptr_t shm_next_virt;
    proc_fd_t fds[PROC_MAX_FD];
    uint32_t magic2; // trailing guard
} process_t;

/* This structure defines the interface of schedulers in SnowflakeOS.
 */
typedef struct _sched_t {
    /* Returns the currently elected process */
    process_t* (*sched_get_current)(struct _sched_t*);
    /* Adds a new process, already initialized, to the process pool */
    void (*sched_add)(struct _sched_t*, process_t*);
    /* Returns the next process that should be run, depending to the specific
       scheduler implemented. Note that it can choose not to change process by
       returning the currently executing process */
    process_t* (*sched_next)(struct _sched_t*);
    /* Removes a process from the process pool. Basically the inverse of
     * `sched_add`. If the removed process was the one currently executing, the
     * scheduler must ensure that `sched_next` keeps working: it'll be called
     * right after.
     */
    void (*sched_exit)(struct _sched_t*, process_t*);
    /* Returns true if a pid exists in the run queue */
    bool (*sched_exists)(struct _sched_t*, uint32_t pid);
} sched_t;

void init_proc();
process_t* proc_run_code(uint8_t* code, uint32_t size, char** argv);
process_t* proc_run_path(const char* path, char** argv);
void proc_print_processes();
void proc_schedule();
void proc_exit();
void proc_enter_usermode();
void proc_switch_process(process_t* next);
uint32_t proc_get_current_pid();

// Currently running process (NULL when running in kernel/idle context).
extern process_t* current_process;

// Cooperative sleep and heap growth for userspace.
void proc_sleep(uint32_t ms);
void* proc_sbrk(intptr_t size);

// Offsets for process_t fields (used by assembly), auto-derived from the struct.
extern uint32_t PROC_OFF_directory;
extern uint32_t PROC_OFF_kernel_stack;
extern uint32_t PROC_OFF_saved_kernel_stack;

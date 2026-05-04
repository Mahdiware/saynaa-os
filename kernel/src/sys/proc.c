#include "kernel/sys/proc.h"

#include "kernel/api/syscall_api.h"
#include "kernel/cpu/fpu.h"
#include "kernel/cpu/gdt.h"
#include "kernel/cpu/timer.h"
#include "kernel/cpu/tss.h"
#include "kernel/fs/vfs.h"
#include "kernel/kernel.h"
#include "kernel/lib/kprintf.h"
#include "kernel/mem/malloc.h"
#include "kernel/mem/paging.h"
#include "kernel/mem/pmm.h"
#include "kernel/sys/elf.h"
#include "kernel/sys/sched_robin.h"
#include "kernel/utils/debug.h"
#include "libc/math.h"
#include "libc/stdio.h"
#include "libc/stdlib.h"
#include "libc/string.h"

extern uint32_t irq_handler_end;

process_t* current_process = NULL;
uint32_t PROC_OFF_directory = offsetof(process_t, directory);
uint32_t PROC_OFF_kernel_stack = offsetof(process_t, kernel_stack);
uint32_t PROC_OFF_saved_kernel_stack = offsetof(process_t, saved_kernel_stack);
sched_t* scheduler = NULL;

static uint32_t next_pid = 1;

static void proc_init_fd_table(process_t* p) {
    memset(p->fds, 0, sizeof(p->fds));

    vfs_node_t* tty = vfs_lookup("/dev/tty");
    if (tty) {
        p->fds[0] = (proc_fd_t) {.used = true, .flags = O_RDONLY, .offset = 0, .node = tty};
        p->fds[1] = (proc_fd_t) {.used = true, .flags = O_WRONLY, .offset = 0, .node = tty};
    }
}

static void proc_inherit_fd_table(process_t* p, const process_t* parent) {
    if (!p || !parent) {
        return;
    }
    memcpy(p->fds, parent->fds, sizeof(p->fds));
}

void init_proc() {
    scheduler = sched_robin();
}

static void proc_stdout_retain(proc_stdout_t* out) {
    if (out) {
        out->refcount++;
    }
}

static void proc_stdout_release(proc_stdout_t* out) {
    if (!out) {
        return;
    }
    if (out->refcount > 0) {
        out->refcount--;
    }
    if (out->refcount == 0) {
        kfree(out);
    }
}

/* Creates a process running the code specified at `code` in raw instructions
 * and add it to the process queue, after the currently executing process.
 * `argv` is the array of arguments, NULL terminated.
 */
process_t* proc_run_code(uint8_t* code, uint32_t size, char** argv) {
    static uintptr_t temp_page = 0;

    if (!temp_page) {
        temp_page = (uintptr_t) kamalloc(0x1000, 0x1000);
    }

    // Save arguments before switching directory and losing them (preserve order)
    list_t args = LIST_HEAD_INIT(args);
    while (argv && *argv) {
        size_t len = strlen(*argv) + 1;
        char* buff = (char*) kmalloc(len);
        if (!buff) {
            break;
        }
        strcpy(buff, *argv);
        list_add(&args, buff);
        argv++;
    }

    // Flat binaries: treat trailing space up to page boundary as zeroed "bss"
    uint32_t num_code_pages = divide_up(size, 0x1000);
    uint32_t num_stack_pages = PROC_STACK_PAGES;

    process_t* process = kmalloc(sizeof(process_t));
    if (!process) {
        kprintf_error("proc: failed to allocate process struct");
        return NULL;
    }

    uintptr_t kernel_stack = (uintptr_t) aligned_alloc(4, 0x1000 * PROC_KERNEL_STACK_PAGES);
    if (!kernel_stack) {
        kprintf_error("proc: failed to allocate kernel stack");
        return NULL;
    }

    uintptr_t pd_phys = pmm_alloc_page();
    if (!pd_phys) {
        kprintf_error("proc: failed to allocate page directory");
        return NULL;
    }

    // Copy the kernel page directory with a temporary mapping
    page_t* p = paging_get_page(temp_page, true, 0);
    *p = pd_phys | PAGE_PRESENT | PAGE_RW;
    paging_invalidate_page(temp_page);
    memcpy((void*) temp_page, (void*) 0xFFFFF000, 0x1000);
    directory_entry_t* pd = (directory_entry_t*) temp_page;
    pd[1023] = pd_phys | PAGE_PRESENT | PAGE_RW;

    // ">> 22" grabs the address's index in the page directory, see `paging.c`
    for (uint32_t i = 0; i < (KERNEL_BASE_VIRT >> 22); i++) {
        pd[i] = 0; // Unmap everything below the kernel
    }

    // Temporarily switch to a fresh page directory to build the new user address space
    page_t* current_pd_page = paging_get_page(0xFFFFF000, false, 0);
    uintptr_t previous_pd = current_pd_page ? (*current_pd_page & PAGE_FRAME) : 0;
    if (!previous_pd) {
        kprintf_error("proc: current page directory missing");
        return NULL;
    }
    paging_switch_directory(pd_phys);

    uintptr_t entry_point = 0x00001000;

    if (elf_is_valid(code, size)) {
        if (elf_load(code, size, &entry_point) != 0) {
            kprintf_error("proc: invalid elf");
            paging_switch_directory(previous_pd);
            return NULL;
        }
    } else {
        // Map the code and copy it to physical pages, zero out the excess
        uintptr_t code_phys = pmm_alloc_pages(num_code_pages);
        paging_map_pages(0x00001000, code_phys, num_code_pages, PAGE_USER | PAGE_RW);
        memcpy((void*) 0x00001000, (void*) code, size);
        memset((uint8_t*) 0x1000 + size, 0, num_code_pages * 0x1000 - size);
    }

    // Map the stack
    uintptr_t stack_phys = pmm_alloc_pages(num_stack_pages);
    paging_map_pages(0xC0000000 - 0x1000 * num_stack_pages, stack_phys, num_stack_pages, PAGE_USER | PAGE_RW);

    /* Setup the (argc, argv) part of the userstack, start by copying the given
     * arguments on that stack. */
    char* ustack_char = (char*) (0xC0000000 - 1);

    char* argv_user[64];
    uint32_t arg_count = 0;
    list_t* iter;
    list_t* n;
    list_for_each_safe(iter, n, &args) {
        char* arg = (char*) iter->data;
        if (arg_count >= 64) {
            kfree(arg);
            list_del(iter);
            continue;
        }
        uint32_t len = (uint32_t) strlen(arg) + 1;
        uintptr_t next = (uintptr_t) ustack_char - len;
        next &= ~0x3; // 4-byte align
        ustack_char = (char*) next;
        strncpy(ustack_char, arg, len);
        argv_user[arg_count++] = ustack_char;
        kfree(arg);
        list_del(iter);
    }

    uint32_t* ustack_int = (uint32_t*) ((uintptr_t) ustack_char & ~0x3);

    *(--ustack_int) = 0; // argv[argc] = NULL
    for (int i = (int) arg_count - 1; i >= 0; i--) {
        *(--ustack_int) = (uint32_t) argv_user[i];
    }

    uint32_t* argv_base = ustack_int;
    *(--ustack_int) = (uint32_t) argv_base; // argv
    *(--ustack_int) = arg_count;            // argc

    // Switch to the original page directory
    paging_switch_directory(previous_pd);

    uint32_t parent = current_process ? current_process->pid : 0;

    const char* parent_cwd = (current_process && current_process->cwd[0]) ? current_process->cwd : "/";

    *process = (process_t) {.magic = 0xC0FEBABE,
        .pid = next_pid++,
        .code_len = num_code_pages,
        .stack_len = num_stack_pages,
        .directory = pd_phys,
        .kernel_stack = kernel_stack + PROC_KERNEL_STACK_PAGES * 0x1000 - 4,
        .saved_kernel_stack = kernel_stack + PROC_KERNEL_STACK_PAGES * 0x1000 - 4,
        .initial_user_stack = (uintptr_t) ustack_int,
        .mem_len = 0,
        .sleep_ticks = 0,
        .parent_pid = parent,
        .shm_next_virt = 0xB0000000,
        .magic2 = 0xC0FEBABE};

    strncpy(process->cwd, parent_cwd, sizeof(process->cwd) - 1);
    process->cwd[sizeof(process->cwd) - 1] = '\0';

    proc_init_fd_table(process);

    if (current_process) {
        proc_inherit_fd_table(process, current_process);
    }

    // Inherit parent's stdout capture if present.
    process->stdout = current_process ? current_process->stdout : NULL;
    proc_stdout_retain(process->stdout);

    // We use this label as the return address from `proc_switch_process`
    uint32_t* jmp = &irq_handler_end;

    // Setup the process's kernel stack as if it had already been interrupted
    asm volatile(
        // Save our stack in %ebx
        "mov %%esp, %%ebx\n"

        // Temporarily use the new process's kernel stack
        "mov %[kstack], %%eax\n"
        "mov %%eax, %%esp\n"

        // Stuff popped by `iret`
        "push $0x23\n" // user ds selector
        "mov %[ustack], %%eax\n"
        "push %%eax\n"    // %esp
        "push $0x202\n"   // %eflags with `IF` bit set
        "push $0x1B\n"    // user cs selector
        "push %[entry]\n" // %eip
        // Push error code, interrupt number
        "sub $8, %%esp\n"
        // `pusha` equivalent
        "sub $32, %%esp\n"
        // push data segment registers
        "mov $0x20, %%eax\n"
        "push %%eax\n"
        "push %%eax\n"
        "push %%eax\n"
        "push %%eax\n"

        // Push proc_switch_process's `ret` %eip
        "mov %[jmp], %%eax\n"
        "push %%eax\n"
        // Push garbage %ebx, %esi, %edi, %ebp
        "push $1\n"
        "push $2\n"
        "push $3\n"
        "push $4\n"

        // Save the new process's %esp in %eax
        "mov %%esp, %%eax\n"
        // Restore our stack
        "mov %%ebx, %%esp\n"
        // Update the new process's %esp
        "mov %%eax, %[esp]\n"
        : [esp] "=r"(process->saved_kernel_stack)
        : [kstack] "r"(process->kernel_stack), [ustack] "r"(process->initial_user_stack), [jmp] "r"(jmp), [entry] "r"(entry_point)
        : "%eax", "%ebx");

    scheduler->sched_add(scheduler, process);

    return process;
}

process_t* proc_run_path(const char* path, char** argv) {
    vfs_node_t* node = vfs_lookup(path);
    if (!node) {
        kprintf_error("proc: cannot find %s", path);
        return NULL;
    }

    if (!(node->flags & VFS_NODE_FILE)) {
        kprintf_error("proc: %s is not a file", path);
        return NULL;
    }

    uint32_t size = node->length;
    if (!size) {
        kprintf_error("proc: %s has zero length", path);
        return NULL;
    }

    uint8_t* buf = kmalloc(size);
    if (!buf) {
        kprintf_error("proc: no memory to load %s", path);
        return NULL;
    }

    ssize_t n = vfs_read(node, 0, size, buf);
    if (n < 0 || (uint32_t) n != size) {
        kprintf_error("proc: failed to read %s", path);
        kfree(buf);
        return NULL;
    }

    process_t* p = proc_run_code(buf, size, argv);
    kfree(buf);
    return p;
}

/* Runs the scheduler. The scheduler may then decide to elect a new process, or
 * not.
 */
void proc_schedule() {
    process_t* next = scheduler->sched_next(scheduler);

    if (!next) {
        current_process = NULL;
        return;
    }

    if (next == current_process) {
        return;
    }

    fpu_switch(current_process, next);
    proc_switch_process(next);
}

/* Called on clock ticks, calls the scheduler.
 */
void proc_timer_callback(REGISTERS* regs) {
    unused(regs);

    proc_schedule();
}

/* Make the first jump to usermode.
 * A special function is needed as our first kernel stack isn't setup to return
 * to any interrupt handler; we have to `iret` ourselves.
 */
void proc_enter_usermode() {
    disable_interrupts(); // Interrupts will be reenabled by `iret`

    current_process = scheduler->sched_get_current(scheduler);

    if (!current_process) {
        kprintf_error("no process to run");
        abort();
    }

    timer_register_callback(&proc_timer_callback);
    set_kernel_stack(current_process->kernel_stack);
    paging_switch_directory(current_process->directory);

    asm volatile("mov $0x23, %%eax\n"
                 "mov %%eax, %%ds\n"
                 "mov %%eax, %%es\n"
                 "mov %%eax, %%fs\n"
                 "mov %%eax, %%gs\n"
                 "push %%eax\n" // %ss
                 "mov %[ustack], %%eax\n"
                 "push %%eax\n"       // %esp
                 "push $0x202\n"      // %eflags with IF set
                 "push $0x1B\n"       // %cs
                 "push $0x00001000\n" // %eip
                 "iret\n" ::[ustack] "r"(current_process->initial_user_stack)
        : "%eax");
}

/* Terminates the currently executing process.
 * Implements the `exit` system call.
 */
void proc_exit() {
    process_t* exiting = current_process;

    proc_stdout_release(exiting->stdout);
    exiting->stdout = NULL;

    scheduler->sched_exit(scheduler, exiting);

    // Pick next process; returns to caller if no switch needed
    proc_schedule();

    // If no process was runnable, halt.
    if (!current_process) {
        kprintf_error("no runnable process after exit");
        infinite_loop();
    }
}

uint32_t proc_get_current_pid() {
    if (current_process) {
        return current_process->pid;
    } else {
        return 0;
    }
}

void proc_sleep(uint32_t ms) {
    if (!current_process) {
        return;
    }

    // Convert milliseconds to scheduler ticks.
    // Use ceil division so non-zero ms never becomes 0 ticks.
    uint32_t ticks = 0;
    if (ms > 0) {
        ticks = (ms * TIMER_FREQ + 999) / 1000;
        if (ticks == 0) {
            ticks = 1;
        }
    }

    current_process->sleep_ticks = ticks;
    proc_schedule();
}

void* proc_sbrk(intptr_t size) {
    if (!current_process) {
        return (void*) -1;
    }

    uintptr_t end = 0x1000 + 0x1000 * current_process->code_len + current_process->mem_len;

    // Bytes available in the last allocated page
    int32_t remaining_bytes = (end % 0x1000) ? (0x1000 - (int32_t) (end % 0x1000)) : 0;

    if (size > 0) {
        if (remaining_bytes < size) {
            uint32_t needed_size = (uint32_t) (size - remaining_bytes);
            uint32_t num = divide_up(needed_size, 0x1000);

            if (!paging_alloc_pages(align_to((uint32_t) end, 0x1000), num)) {
                return (void*) -1;
            }
        }
    } else if (size < 0) {
        if ((intptr_t) (end + (uintptr_t) size) < (intptr_t) (0x1000 * current_process->code_len)) {
            return (void*) -1;
        }

        int32_t taken = 0x1000 - remaining_bytes;

        // We must free at least a page
        if (taken + size < 0) {
            uint32_t freed_size = (uint32_t) (taken - size);
            uint32_t num = divide_up(freed_size, 0x1000);

            uintptr_t virt = end - (end % 0x1000);
            for (uint32_t i = 0; i < num; i++) {
                paging_unmap_page(virt - 0x1000 * i);
            }
        }
    }

    current_process->mem_len = (uint32_t) ((intptr_t) current_process->mem_len + size);
    return (void*) end;
}

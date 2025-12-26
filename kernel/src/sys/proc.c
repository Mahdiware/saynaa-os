#include "kernel/sys/proc.h"

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
sched_t* scheduler = NULL;

static uint32_t next_pid = 1;

void init_proc() {
    scheduler = sched_robin();
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
    uintptr_t kernel_stack = (uintptr_t) aligned_alloc(4, 0x1000 * PROC_KERNEL_STACK_PAGES);
    uintptr_t pd_phys = pmm_alloc_page();

    // Copy the kernel page directory with a temporary mapping
    page_t* p = paging_get_page(temp_page, false, 0);
    *p = pd_phys | PAGE_PRESENT | PAGE_RW;
    memcpy((void*) temp_page, (void*) 0xFFFFF000, 0x1000);
    directory_entry_t* pd = (directory_entry_t*) temp_page;
    pd[1023] = pd_phys | PAGE_PRESENT | PAGE_RW;

    // ">> 22" grabs the address's index in the page directory, see `paging.c`
    for (uint32_t i = 0; i < (KERNEL_BASE_VIRT >> 22); i++) {
        pd[i] = 0; // Unmap everything below the kernel
    }

    // We can now switch to that directory to modify it easily
    uintptr_t previous_pd = *paging_get_page(0xFFFFF000, false, 0) & PAGE_FRAME;
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

    *process = (process_t) {.pid = next_pid++,
        .code_len = num_code_pages,
        .stack_len = num_stack_pages,
        .directory = pd_phys,
        .kernel_stack = kernel_stack + PROC_KERNEL_STACK_PAGES * 0x1000 - 4,
        .saved_kernel_stack = kernel_stack + PROC_KERNEL_STACK_PAGES * 0x1000 - 4,
        .initial_user_stack = (uintptr_t) ustack_int,
        .sleep_ticks = 0,
        .parent_pid = parent};

    strncpy(process->cwd, parent_cwd, sizeof(process->cwd) - 1);
    process->cwd[sizeof(process->cwd) - 1] = '\0';

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

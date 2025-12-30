#include "kernel/sys/sched_robin.h"

#include "kernel/mem/malloc.h"

/* Wraps a `process_t*` for round robin purposes.
 */
typedef struct _proc_node_t {
    process_t* process;
    struct _proc_node_t* next;
} proc_node_t;

/* The round robin scheduler is simple and requires only a single circular list
 * containing candidate processes. By having a `sched_t` as the first member of
 * the struct, we allow casting `sched_robin_t*`s to `sched_t*`.
 */
typedef struct {
    sched_t sched;
    proc_node_t* processes;
} sched_robin_t;

static bool proc_valid(process_t* p) {
    return p && p->directory && p->saved_kernel_stack && p->magic == 0xC0FEBABE && p->magic2 == 0xC0FEBABE;
}

// Prune must be called with interrupts disabled by the caller.
static void sched_robin_prune(sched_robin_t* sc) {
    if (!sc->processes) {
        return;
    }

    // Drop invalid head nodes until head is valid or list becomes empty.
    while (sc->processes && !proc_valid(sc->processes->process)) {
        proc_node_t* head = sc->processes;
        uint32_t pid = head->process ? head->process->pid : 0;
        kprintf_error("sched: prune head node=%p proc=%p pid=%u dir=%p kstack=%p", (void*) head,
            (void*) head->process, pid, head->process ? head->process->directory : -1,
            head->process ? (void*) head->process->saved_kernel_stack : NULL);
        if (head->next == head) {
            kprintf_error("sched: dropped invalid process pid=%u dir=%p kstack=%p", pid,
                head->process ? head->process->directory : -1,
                head->process ? (void*) head->process->saved_kernel_stack : NULL);
            kfree(head);
            sc->processes = NULL;
            return;
        }

        // Find tail to relink the ring.
        proc_node_t* tail = head;
        while (tail->next != head) {
            tail = tail->next;
        }

        kprintf_error("sched: dropped invalid process pid=%u dir=%p kstack=%p", pid,
            head->process ? head->process->directory : 0,
            head->process ? (void*) head->process->saved_kernel_stack : NULL);

        tail->next = head->next;
        sc->processes = head->next;
        kfree(head);
    }

    if (!sc->processes) {
        return;
    }

    // Walk remaining ring and remove invalid nodes.
    proc_node_t* prev = sc->processes;
    proc_node_t* cur = prev->next;

    while (cur != sc->processes) {
        if (!proc_valid(cur->process)) {
            uint32_t pid = cur->process ? cur->process->pid : 0;
            kprintf_error("sched: prune node=%p proc=%p pid=%u dir=%p kstack=%p", (void*) cur,
                (void*) cur->process, pid, cur->process ? cur->process->directory : 0,
                cur->process ? (void*) cur->process->saved_kernel_stack : NULL);
            kprintf_error("sched: dropped invalid process pid=%u dir=%p kstack=%p", pid,
                cur->process ? cur->process->directory : 0,
                cur->process ? (void*) cur->process->saved_kernel_stack : NULL);
            prev->next = cur->next;
            kfree(cur);
            cur = prev->next;
            continue;
        }
        prev = cur;
        cur = cur->next;
    }
}

process_t* sched_robin_get_current(sched_t* sched) {
    sched_robin_t* sc = (sched_robin_t*) sched;
    uint32_t flags = irq_save();
    sched_robin_prune(sc);
    irq_restore(flags);

    if (sc->processes) {
        return sc->processes->process;
    }

    return NULL;
}

void sched_robin_add(sched_t* sched, process_t* new_process) {
    sched_robin_t* sc = (sched_robin_t*) sched;

    if (!new_process || !new_process->directory || !new_process->saved_kernel_stack) {
        kprintf_error("sched: refused to add invalid process pid=%u dir=%p kstack=%p",
            new_process ? new_process->pid : 0, new_process ? new_process->directory : 0,
            new_process ? (void*) new_process->saved_kernel_stack : NULL);
        return;
    }

    uint32_t flags = irq_save();
    sched_robin_prune(sc);

    proc_node_t* new = kmalloc(sizeof(proc_node_t));
    kprintf_info("sched: add node=%p proc=%p pid=%u dir=%p kstack=%p", (void*) new, (void*) new_process,
        new_process->pid, (void*) new_process->directory, (void*) new_process->saved_kernel_stack);

    new->process = new_process;

    // Insert the process in the ring, create it if empty
    if (!sc->processes) {
        new->next = new;
        sc->processes = new;
    } else {
        proc_node_t* p = sc->processes->next;
        sc->processes->next = new;
        new->next = p;
    }
    irq_restore(flags);
}

static bool sched_robin_exists(sched_t* sched, uint32_t pid) {
    sched_robin_t* sc = (sched_robin_t*) sched;
    proc_node_t* p = sc->processes;

    if (!p) {
        return false;
    }

    proc_node_t* start = p;
    do {
        if (p->process && p->process->pid == pid) {
            return true;
        }
        p = p->next;
    } while (p && p != start);

    return false;
}

process_t* sched_robin_next(sched_t* sched) {
    sched_robin_t* sc = (sched_robin_t*) sched;
    uint32_t flags = irq_save();
    sched_robin_prune(sc);

    proc_node_t* p = sc->processes;

    if (!p) {
        irq_restore(flags);
        return NULL;
    }

    // Avoid switching to a sleeping process if possible
    do {
        if (p->next->process->sleep_ticks > 0) {
            p->next->process->sleep_ticks--;
        } else {
            // We don't need to switch process
            if (p->next == sc->processes) {
                process_t* ret = sc->processes->process;
                irq_restore(flags);
                return ret;
            }

            // We don't need to modify the process queue
            if (p->next == sc->processes->next) {
                break;
            }

            // We insert the next process between the current one and the one
            // previously scheduled to be switched to.
            proc_node_t* previous = p;
            proc_node_t* next_proc = p->next;
            proc_node_t* moved = sc->processes->next;

            previous->next = next_proc->next;
            next_proc->next = moved;
            sc->processes->next = next_proc;

            break;
        }

        p = p->next;
    } while (p != sc->processes);

    sc->processes = sc->processes->next;

    process_t* ret = sc->processes->process;
    irq_restore(flags);
    return ret;
}

void sched_robin_exit(sched_t* sched, process_t* process) {
    sched_robin_t* sc = (sched_robin_t*) sched;
    uint32_t flags = irq_save();

    proc_node_t* p = sc->processes;

    if (!p) {
        irq_restore(flags);
        return;
    }

    if (sc->processes == sc->processes->next) {
        kfree(sc->processes);
        sc->processes = NULL;
        irq_restore(flags);
        return;
    }

    proc_node_t* start = p;
    while (p->next->process != process) {
        p = p->next;
        if (p == start) {
            // Not found; nothing to remove.
            irq_restore(flags);
            return;
        }
    }

    proc_node_t* to_remove = p->next;
    p->next = p->next->next;

    sc->processes = p;

    kfree(to_remove);

    irq_restore(flags);
}

/* Allocates a round robin scheduler.
 */
sched_t* sched_robin() {
    sched_robin_t* sched = kmalloc(sizeof(sched_robin_t));

    sched->sched = (sched_t) {.sched_get_current = sched_robin_get_current,
        .sched_add = sched_robin_add,
        .sched_next = sched_robin_next,
        .sched_exit = sched_robin_exit,
        .sched_exists = sched_robin_exists};

    sched->processes = NULL;

    return (sched_t*) sched;
}
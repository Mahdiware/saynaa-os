#include "kernel/utils/spinlock.h"

static inline void cpu_relax(void) {
    asm volatile("pause" ::: "memory");
}

static inline uint32_t test_and_set(volatile uint32_t* addr) {
    uint32_t prev = 1;
    asm volatile("lock xchg %0, %1" : "+r"(prev), "+m"(*addr) : : "memory");
    return prev;
}

void spinlock_init(spinlock_t* lock) {
    if (lock) {
        lock->value = 0;
    }
}

void spinlock_lock(spinlock_t* lock) {
    while (test_and_set(&lock->value)) {
        while (lock->value) {
            cpu_relax();
        }
    }
}

bool spinlock_try_lock(spinlock_t* lock) {
    return test_and_set(&lock->value) == 0;
}

void spinlock_unlock(spinlock_t* lock) {
    asm volatile("" ::: "memory");
    lock->value = 0;
}

uint32_t spinlock_lock_irqsave(spinlock_t* lock) {
    uint32_t flags = irq_save();
    spinlock_lock(lock);
    return flags;
}

void spinlock_unlock_irqrestore(spinlock_t* lock, uint32_t flags) {
    spinlock_unlock(lock);
    irq_restore(flags);
}

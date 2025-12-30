#pragma once

#include "kernel/kernel.h"
#include "libc/stdint.h"

typedef struct {
    volatile uint32_t value;
} spinlock_t;

#define SPINLOCK_INIT {.value = 0}

void spinlock_init(spinlock_t* lock);
void spinlock_lock(spinlock_t* lock);
bool spinlock_try_lock(spinlock_t* lock);
void spinlock_unlock(spinlock_t* lock);
uint32_t spinlock_lock_irqsave(spinlock_t* lock);
void spinlock_unlock_irqrestore(spinlock_t* lock, uint32_t flags);

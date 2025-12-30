#pragma once

#include "libc/stdint.h"

// symbols from linker.ld for section addresses
extern uint8_t __kernel_start_virt__;
extern uint8_t __kernel_start_phys__;
extern uint8_t __kernel_end_virt__;
extern uint8_t __kernel_end_phys__;
extern uint8_t __kernel_size__;

#define enable_interrupts() asm volatile("sti")
#define disable_interrupts() asm volatile("cli")
#define halt() asm volatile("hlt")
#define unused(p) (void) p

static inline uint32_t irq_save(void) {
    uint32_t flags;
    asm volatile("pushf; pop %0; cli" : "=r"(flags)::"memory");
    return flags;
}

static inline void irq_restore(uint32_t flags) {
    asm volatile("push %0; popf" ::"r"(flags) : "memory", "cc");
}

#define infinite_loop() \
    while (1) { \
        halt(); \
    }

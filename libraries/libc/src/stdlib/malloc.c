#include "libc/stdint.h"
#include "libc/stdlib.h"
#include "libc/string.h"
#include "libc/syscall.h"

#define MIN_ALIGN 4

#ifndef offsetof
#define offsetof(type, member) __builtin_offsetof(type, member)
#endif

typedef struct _mem_block_t {
    struct _mem_block_t* next;
    uint32_t size; // last bit is used flag
    uint8_t data[1];
} mem_block_t;

static mem_block_t* g_bottom = NULL;
static mem_block_t* g_top = NULL;

static uint32_t align_to_local(uint32_t n, uint32_t align) {
    if (align == 0) {
        return n;
    }
    if (n % align == 0) {
        return n;
    }
    return n + (align - n % align);
}

static uint32_t mem_block_payload_size(const mem_block_t* block) {
    return (uint32_t) (block->size & ~1u);
}

static uint32_t mem_block_total_size(const mem_block_t* block) {
    return (uint32_t) sizeof(mem_block_t) + mem_block_payload_size(block);
}

static mem_block_t* mem_get_block(void* pointer) {
    uintptr_t addr = (uintptr_t) pointer;
    return (mem_block_t*) (addr - sizeof(mem_block_t) + 4);
}

static bool mem_is_aligned(const mem_block_t* block, uint32_t align) {
    uintptr_t addr = (uintptr_t) block->data;
    return (addr % align) == 0;
}

static void* sbrk_local(int32_t size) {
    // Kernel returns the new program break as an address-sized integer; cast to pointer here
    return (void*) (uintptr_t) syscall1(SYS_SBRK, (uint32_t) size);
}

static mem_block_t* mem_find_block(uint32_t size, uint32_t align) {
    if (!g_bottom) {
        return NULL;
    }

    mem_block_t* block = g_bottom;
    while ((mem_block_payload_size(block) < size) || (block->size & 1u) || !mem_is_aligned(block, align)) {
        block = block->next;
        if (!block) {
            return NULL;
        }
    }

    return block;
}

static mem_block_t* mem_new_block(uint32_t size, uint32_t align) {
    const uint32_t header_size = (uint32_t) offsetof(mem_block_t, data);

    uintptr_t next = (uintptr_t) g_top + mem_block_total_size(g_top);
    uintptr_t next_aligned = (uintptr_t) align_to_local((uint32_t) (next + header_size), align) - header_size;

    mem_block_t* block = (mem_block_t*) next_aligned;
    block->size = size | 1u;
    block->next = NULL;

    // Insert a filler free block if there is space.
    next = (uintptr_t) align_to_local((uint32_t) (next + header_size), MIN_ALIGN) - header_size;
    if (next_aligned - next > sizeof(mem_block_t) + MIN_ALIGN) {
        mem_block_t* filler = (mem_block_t*) next;
        filler->size = (uint32_t) (next_aligned - next - sizeof(mem_block_t));
        g_top->next = filler;
        g_top = filler;
    }

    g_top->next = block;
    g_top = block;

    return block;
}

void* aligned_alloc(size_t align, size_t size) {
    const uint32_t header_size = (uint32_t) offsetof(mem_block_t, data);
    uint32_t req = align_to_local((uint32_t) size, 8);

    if (!g_top) {
        uintptr_t addr = (uintptr_t) sbrk_local((int32_t) header_size);
        if (addr == (uintptr_t) -1) {
            return NULL;
        }
        g_bottom = (mem_block_t*) addr;
        g_top = g_bottom;
        g_top->size = 1u; // used, size 0
        g_top->next = NULL;
    }

    mem_block_t* block = mem_find_block(req, (uint32_t) align);
    if (block) {
        block->size |= 1u;
        return block->data;
    }

    uintptr_t end = (uintptr_t) g_top + mem_block_total_size(g_top) + header_size;
    end = (uintptr_t) align_to_local((uint32_t) end, (uint32_t) align) + req;

    uintptr_t brk = (uintptr_t) sbrk_local(0);
    if (end > brk) {
        if (sbrk_local((int32_t) (end - brk)) == (void*) -1) {
            return NULL;
        }
    }

    block = mem_new_block(req, (uint32_t) align);
    return block->data;
}

void* malloc(size_t size) {
    return aligned_alloc(MIN_ALIGN, size);
}

void free(void* pointer) {
    if (!pointer) {
        return;
    }
    mem_block_t* block = mem_get_block(pointer);
    block->size &= ~1u;
}

void* calloc(size_t nmemb, size_t size) {
    void* ptr = malloc(nmemb * size);
    if (!ptr) {
        return NULL;
    }
    memset(ptr, 0, (uint32_t) (nmemb * size));
    return ptr;
}

void* zalloc(size_t size) {
    return calloc(1, size);
}

void* realloc(void* ptr, size_t size) {
    if (!ptr) {
        return malloc(size);
    }
    if (!size) {
        free(ptr);
        return NULL;
    }

    void* n = malloc(size);
    if (!n) {
        return NULL;
    }

    uint32_t old = mem_block_payload_size(mem_get_block(ptr));
    uint32_t copy = old;
    if (copy > (uint32_t) size) {
        copy = (uint32_t) size;
    }
    memcpy(n, ptr, copy);
    free(ptr);
    return n;
}

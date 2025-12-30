#include "kernel/mem/shm.h"

#include "kernel/mem/malloc.h"
#include "kernel/mem/paging.h"
#include "kernel/mem/pmm.h"
#include "kernel/sys/proc.h"
#include "kernel/utils/debug.h"
#include "libc/math.h"

#define SHM_MAX_OBJECTS 64

typedef struct shm_obj {
    uint32_t id;
    uint32_t size_bytes;
    uint32_t num_pages;
    uintptr_t* pages_phys; // array[num_pages]
    uint32_t refcount;
} shm_obj_t;

static shm_obj_t shm_objs[SHM_MAX_OBJECTS] = {0};
static uint32_t shm_next_id = 1;

static shm_obj_t* shm_find(uint32_t id) {
    for (uint32_t i = 0; i < SHM_MAX_OBJECTS; i++) {
        if (shm_objs[i].id == id) {
            return &shm_objs[i];
        }
    }
    return NULL;
}

static shm_obj_t* shm_alloc_slot(void) {
    for (uint32_t i = 0; i < SHM_MAX_OBJECTS; i++) {
        if (shm_objs[i].id == 0) {
            return &shm_objs[i];
        }
    }
    return NULL;
}

int shm_create(uint32_t size_bytes, uint32_t* out_id) {
    if (!out_id || size_bytes == 0) {
        return -1;
    }

    shm_obj_t* obj = shm_alloc_slot();
    if (!obj) {
        return -1;
    }

    uint32_t num_pages = divide_up(size_bytes, 0x1000);
    uintptr_t* pages = (uintptr_t*) kmalloc(sizeof(uintptr_t) * num_pages);
    if (!pages) {
        return -1;
    }

    for (uint32_t i = 0; i < num_pages; i++) {
        uintptr_t phys = pmm_alloc_page();
        if (!phys) {
            for (uint32_t j = 0; j < i; j++) {
                pmm_free_page(pages[j]);
            }
            kfree(pages);
            return -1;
        }
        pages[i] = phys;
    }

    uint32_t id = shm_next_id++;
    if (id == 0) {
        id = shm_next_id++;
    }

    *obj = (shm_obj_t) {
        .id = id,
        .size_bytes = size_bytes,
        .num_pages = num_pages,
        .pages_phys = pages,
        .refcount = 1,
    };

    *out_id = id;
    return 0;
}

void* shm_map(uint32_t id) {
    if (!current_process) {
        return NULL;
    }

    shm_obj_t* obj = shm_find(id);
    if (!obj || obj->id == 0 || obj->num_pages == 0 || !obj->pages_phys) {
        return NULL;
    }

    // Reserve a contiguous virtual range in the process.
    // We map from high user space downward (below the user stack region).
    uintptr_t next = current_process->shm_next_virt;
    if (next == 0) {
        next = 0xB0000000;
        current_process->shm_next_virt = next;
    }

    uint32_t bytes = obj->num_pages * 0x1000;
    uintptr_t base = next - bytes;
    base = align_to(base, 0x1000);

    // Map each physical page into the current address space.
    for (uint32_t i = 0; i < obj->num_pages; i++) {
        uintptr_t v = base + i * 0x1000;
        page_t* p = paging_get_page(v, true, PAGE_RW | PAGE_USER);
        if (!p) {
            return NULL;
        }
        if ((*p) & PAGE_PRESENT) {
            // This is unexpected (collision). Fail.
            return NULL;
        }
        *p = (obj->pages_phys[i] & PAGE_FRAME) | PAGE_PRESENT | PAGE_RW | PAGE_USER;
        paging_invalidate_page(v);
    }

    current_process->shm_next_virt = base;
    obj->refcount++;

    return (void*) base;
}

int shm_close(uint32_t id) {
    shm_obj_t* obj = shm_find(id);
    if (!obj || obj->id == 0) {
        return -1;
    }

    if (obj->refcount > 0) {
        obj->refcount--;
    }

    if (obj->refcount == 0) {
        if (obj->pages_phys) {
            for (uint32_t i = 0; i < obj->num_pages; i++) {
                if (obj->pages_phys[i]) {
                    pmm_free_page(obj->pages_phys[i]);
                }
            }
            kfree(obj->pages_phys);
        }
        *obj = (shm_obj_t) {0};
    }

    return 0;
}

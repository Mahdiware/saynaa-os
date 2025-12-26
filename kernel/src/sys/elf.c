#include "kernel/sys/elf.h"

#include "kernel/mem/paging.h"
#include "kernel/mem/pmm.h"
#include "libc/math.h"
#include "libc/string.h"

bool elf_is_valid(const uint8_t* buf, uint32_t size) {
    if (!buf || size < sizeof(Elf32_Ehdr)) {
        return false;
    }

    const Elf32_Ehdr* eh = (const Elf32_Ehdr*) buf;

    return eh->e_ident[0] == ELFMAG0 && eh->e_ident[1] == ELFMAG1 && eh->e_ident[2] == ELFMAG2
           && eh->e_ident[3] == ELFMAG3 && eh->e_ident[4] == ELFCLASS32 && eh->e_ident[5] == ELFDATA2LSB
           && eh->e_machine == EM_386 && eh->e_type == ET_EXEC && eh->e_version == EV_CURRENT;
}

int elf_load(const uint8_t* buf, uint32_t size, uintptr_t* entry_out) {
    if (!elf_is_valid(buf, size)) {
        return -1;
    }

    const Elf32_Ehdr* eh = (const Elf32_Ehdr*) buf;

    if (eh->e_phoff + (uint32_t) eh->e_phnum * sizeof(Elf32_Phdr) > size) {
        return -1;
    }

    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        const Elf32_Phdr* ph = (const Elf32_Phdr*) (buf + eh->e_phoff + i * sizeof(Elf32_Phdr));
        if (ph->p_type != PT_LOAD) {
            continue;
        }
        if (ph->p_offset + ph->p_filesz > size) {
            return -1;
        }

        uintptr_t vaddr = ph->p_vaddr;
        uintptr_t page_base = vaddr & ~0xFFF;
        uint32_t page_off = vaddr & 0xFFF;
        uint32_t total = ph->p_memsz + page_off;
        uint32_t pages = divide_up(total, 0x1000);

        uintptr_t phys = pmm_alloc_pages(pages);
        paging_map_pages(page_base, phys, pages, PAGE_USER | PAGE_RW);

        memcpy((void*) vaddr, buf + ph->p_offset, ph->p_filesz);
        if (ph->p_memsz > ph->p_filesz) {
            memset((void*) (vaddr + ph->p_filesz), 0, ph->p_memsz - ph->p_filesz);
        }
    }

    if (entry_out) {
        *entry_out = eh->e_entry;
    }

    return 0;
}

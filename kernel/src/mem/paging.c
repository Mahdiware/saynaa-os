#include "kernel/mem/paging.h"

#include "kernel/cpu/serial.h"
#include "kernel/sys/proc.h"
#include "kernel/utils/debug.h"
#include "libc/math.h"
#include "libc/string.h"

#define DIRECTORY_INDEX(x) ((x) >> 22)
#define TABLE_INDEX(x) (((x) >> 12) & 0x3FF)

static directory_entry_t* current_page_directory;

extern directory_entry_t initial_page_dir[1024];

/**
 * Initializes paging by setting up the page directory and mapping initial pages.
 *
 * @param boot Pointer to the multiboot2 structure provided by the bootloader.
 */
void init_paging(mb2_t* boot) {
    isr_register_handler(14, &paging_fault_handler);

    // Setup the recursive page directory entry
    uintptr_t dir_phys = VIRT_TO_PHYS((uintptr_t) &initial_page_dir);
    initial_page_dir[1023] = dir_phys | PAGE_PRESENT | PAGE_RW;
    paging_invalidate_page(0xFFC00000);

    // Replace the initial identity mapping, extending it to cover grub modules
    uint32_t end = max((uintptr_t) boot + boot->total_size, pmm_get_kernel_end());
    uint32_t to_map = divide_up(end, 0x1000);
    memset(initial_page_dir, 0, (DIRECTORY_INDEX(KERNEL_BASE_VIRT) - 1) * sizeof(directory_entry_t));

    paging_map_pages(0x00000000, 0x00000000, to_map, PAGE_RW);
    paging_invalidate_page(0x00000000);
    current_page_directory = initial_page_dir;
}

/**
 * @brief Retrieves the physical address of the initial page directory.
 *
 * This function converts the virtual address of the initial page directory
 * to its corresponding physical address and returns it.
 *
 * @return The physical address of the initial page directory.
 */
uintptr_t paging_get_initial_page_dir() {
    return VIRT_TO_PHYS((uintptr_t) &initial_page_dir);
}

/**
 * @brief Retrieves a pointer to the page table entry for a given virtual address.
 *
 * This function takes a page-aligned virtual address and returns a pointer to the
 * corresponding page table entry. The entry can then be filled with appropriate
 * page information such as the physical address it points to, whether it is writable, etc.
 *
 * @param virt The page-aligned virtual address for which to retrieve the page table entry.
 * @param create If true, the corresponding page table is created with the passed flags if needed.
 *               This function will never return NULL if this flag is set.
 * @param flags The flags to use when creating a new page table entry.
 * @return A pointer to the page table entry corresponding to the given virtual address,
 *         or NULL if the page table does not exist and the `create` flag is not set.
 */
page_t* paging_get_page(uintptr_t virt, bool create, uint32_t flags) {
    if (virt % 0x1000) {
        kprintf_error("Paging_get_page: unaligned address %x", virt);
        abort();
    }

    uint32_t dir_index = DIRECTORY_INDEX(virt);
    uint32_t table_index = TABLE_INDEX(virt);

    directory_entry_t* dir = (directory_entry_t*) 0xFFFFF000;
    page_t* table = (page_t*) (0xFFC00000 + (dir_index << 12));

    if (!(dir[dir_index] & PAGE_PRESENT) && create) {
        page_t* new_table = (page_t*) pmm_alloc_page();
        dir[dir_index] = (uint32_t) new_table | PAGE_PRESENT | PAGE_RW | (flags & PAGE_FLAGS);
        asm volatile("invlpg (%0)" : : "b"(table) : "memory");
        memset((void*) table, 0, 4096);
    }

    if (dir[dir_index] & PAGE_PRESENT) {
        return &table[table_index];
    }

    return NULL;
}

/**
 * @brief Maps a physical address to a virtual address in the paging system.
 *
 * This function maps a given physical address to a specified virtual address
 * with the provided flags. It first retrieves the page entry for the virtual
 * address. If the virtual address is already mapped, it logs an error and
 * aborts the operation. Otherwise, it sets the page entry to the physical
 * address with the appropriate flags and invalidates the page to ensure the
 * changes take effect.
 *
 * @param virt The virtual address to map.
 * @param phys The physical address to map to the virtual address.
 * @param flags The flags to set for the page (e.g., read/write permissions).
 */
// TODO: refuse 4 MiB pages
void paging_map_page(uintptr_t virt, uintptr_t phys, uint32_t flags) {
    page_t* page = paging_get_page(virt, true, flags);

    if (*page & PAGE_PRESENT) {
        kprintf_error("tried to map an already mapped virtual address 0x%x to 0x%x", virt, phys);
        kprintf_error("previous mapping: 0x%x to 0x%x", virt, *page & PAGE_FRAME);
        abort();
    }

    *page = phys | PAGE_PRESENT | (flags & PAGE_FLAGS);
    paging_invalidate_page(virt);
}

/**
 * @brief Unmaps a virtual page from the paging system.
 *
 * This function takes a virtual address and unmaps the corresponding page
 * from the paging system. It first retrieves the page table entry for the
 * given virtual address. If the page table entry exists, it frees the
 * physical memory associated with the page and then clears the page table entry.
 *
 * @param virt The virtual address of the page to unmap.
 */
void paging_unmap_page(uintptr_t virt) {
    page_t* page = paging_get_page(virt, false, 0);

    if (page) {
        pmm_free_page(*page & PAGE_FRAME);
        *page = 0;
    }
}

/**
 * @brief Maps a range of virtual addresses to physical addresses in the paging system.
 *
 * This function maps a specified number of pages starting from the given virtual
 * and physical addresses. Each page is mapped with the provided flags.
 *
 * @param virt The starting virtual address to map.
 * @param phys The starting physical address to map.
 * @param num The number of pages to map.
 * @param flags The flags to set for each page mapping (e.g., read/write permissions).
 */
void paging_map_pages(uintptr_t virt, uintptr_t phys, uint32_t num, uint32_t flags) {
    for (uint32_t i = 0; i < num; i++) {
        paging_map_page(virt, phys, flags);
        phys += 0x1000;
        virt += 0x1000;
    }
}

/**
 * @brief Unmaps a range of pages starting from a given virtual address.
 *
 * This function unmaps a specified number of pages starting from the given
 * virtual address. It iterates through each page, unmapping them one by one.
 *
 * @param virt The starting virtual address of the pages to be unmapped.
 * @param num The number of pages to unmap.
 */
void paging_unmap_pages(uintptr_t virt, uint32_t num) {
    for (uint32_t i = 0; i < num; i++) {
        paging_unmap_page(virt);
        virt += 0x1000;
    }
}

/**
 * @brief Switches the current page directory to a new one.
 *
 * This function updates the CR3 register to point to a new page directory.
 * The CR3 register holds the physical address of the page directory, which
 * is used by the CPU for virtual memory address translation.
 *
 * @param dir_phys The physical address of the new page directory.
 */
void paging_switch_directory(uintptr_t dir_phys) {
    asm volatile("mov %0, %%cr3\n" ::"r"(dir_phys));
}

/**
 * @brief Invalidates the CPU cache by reloading the CR3 register.
 *
 * This function forces the CPU to reload the page directory by writing
 * the current value of the CR3 register back into itself. This effectively
 * invalidates the CPU cache, ensuring that any changes to the page tables
 * are recognized by the CPU.
 */
void paging_invalidate_cache() {
    asm("mov %cr3, %eax\n"
        "mov %eax, %cr3\n");
}

/**
 * @brief Invalidates a single page in the TLB (Translation Lookaside Buffer).
 *
 * This function uses the `invlpg` assembly instruction to invalidate the TLB entry
 * for the specified virtual address. This is necessary when the page tables are
 * modified and the changes need to be reflected in the TLB.
 *
 * @param virt The virtual address of the page to invalidate.
 */
void paging_invalidate_page(uintptr_t virt) {
    asm volatile("invlpg (%0)" ::"b"(virt) : "memory");
}

/**
 * @brief Handles page faults that occur during program execution.
 *
 * This function is called when a page fault occurs. It retrieves the error code
 * and the address that caused the fault, and logs detailed information about
 * the fault, including the instruction address, process ID, and the nature of
 * the fault (e.g., read/write, user/kernel mode). It also checks if the page
 * was present and if any reserved bits were overwritten. If the fault occurred
 * during an instruction fetch, this is also logged. Finally, the function
 * aborts the current process.
 *
 * @param regs Pointer to the register state at the time of the fault.
 */
void paging_fault_handler(REGISTERS* regs) {
    if (!regs) {
        kprintf_error("weird page fault");
        abort();
    }

    uint32_t err = regs->err_code;
    uint32_t pid = current_process->pid;
    uintptr_t cr2 = 0;
    asm volatile("mov %%cr2, %0\n" : "=r"(cr2));

    kprintf_error("page fault caused by instruction at 0x%x from process %d:", regs->eip, pid);
    kprintf_error("the page at 0x%x %s present ", cr2, err & 0x01 ? "was" : "wasn't");
    kprintf_error("when a process tried to %s it", err & 0x02 ? "write to" : "read from");
    kprintf_error("this process was in %s mode", err & 0x04 ? "user" : "kernel");

    page_t* page = paging_get_page(cr2 & PAGE_FRAME, false, 0);

    if (page) {
        if (err & 0x01) {
            kprintf_error("The page was in %s mode", (*page) & PAGE_USER ? "user" : "kernel");
        }
    }

    if (err & 0x08) {
        kprintf_error("The reserved bits were overwritten");
    }

    if (err & 0x10) {
        kprintf_error("The fault occured during an instruction fetch");
    }

    if (pid) {
        proc_exit();
    } else {
        abort();
    }
}

/* Allocates `num` pages of physical memory, mapped starting at `virt`.
 * Note: pages allocated by this function are not mapped across processes.
 */
void* paging_alloc_pages(uint32_t virt, uint32_t size) {
    for (uint32_t i = 0; i < size; i++) {
        uintptr_t page = pmm_alloc_page();

        if (!page) {
            return NULL;
        }

        page_t* p = paging_get_page(virt + i * 0x1000, true, PAGE_RW | PAGE_USER);
        *p = page | PAGE_PRESENT | PAGE_RW | PAGE_USER;
    }

    return (void*) virt;
}

/* Returns the current physical mapping of `virt` if it exists, zero
 * otherwise.
 */
uintptr_t paging_virt_to_phys(uintptr_t virt) {
    page_t* p = paging_get_page(virt & PAGE_FRAME, false, 0);

    if (!p) {
        return 0;
    }

    return (((uintptr_t) *p) & PAGE_FRAME) + (virt & 0xFFF);
}
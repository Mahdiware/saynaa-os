#include "kernel/kernel.h"

#include "kernel/boot/multiboot2.h"
#include "kernel/cpu/fpu.h"
#include "kernel/cpu/gdt.h"
#include "kernel/cpu/idt.h"
#include "kernel/cpu/serial.h"
#include "kernel/cpu/timer.h"
#include "kernel/drivers/keyboard.h"
#include "kernel/fs/dev/dev_console.h"
#include "kernel/fs/dev/dev_null.h"
#include "kernel/fs/dev/dev_tty.h"
#include "kernel/fs/dev/dev_zero.h"
#include "kernel/fs/ext2.h"
#include "kernel/fs/vfs.h"
#include "kernel/lib/fb.h"
#include "kernel/lib/kprintf.h"
#include "kernel/mem/malloc.h"
#include "kernel/mem/paging.h"
#include "kernel/mem/pmm.h"
#include "kernel/sys/proc.h"
#include "kernel/sys/syscall.h"
#include "libc/math.h"
#include "libc/stdio.h"
#include "libc/string.h"

static void shell_loop(void);
static bool make_abs_path(const char* cwd, const char* input, char* out, size_t out_len);
static bool normalize_path(char* path);

void kernel_main(mb2_t* boot, uint32_t magic) {
    init_serial();
    init_fpu();
    init_gdt();
    init_idt();
    init_timer();
    init_pmm(boot);
    init_paging(boot);

    vfs_init();
    vfs_register_fs(&ext2_driver);

    init_fb(boot);
    set_text_color(vga_to_color(15), vga_to_color(0));
    init_keyboard();
    init_dev_null();
    init_dev_zero();
    init_dev_tty();
    init_dev_console();

    init_proc();
    init_syscall();

    if (magic != MB2_MAGIC) {
        kprintf("warning: invalid magic number from GRUB (%p)!", magic);
    }

    kprintf("Saynaa OS, from scratch\n\n");

    /* Load GRUB modules: the disk image, and symbol file for stacktraces */
    mb2_tag_t* tag = boot->tags;

    while (tag->type != MB2_TAG_END) {
        if (tag->type == MB2_TAG_MODULE) {
            mb2_tag_module_t* mod = (mb2_tag_module_t*) tag;
            uint32_t size = mod->mod_end - mod->mod_start;
            char* module_name = (char*) mod->name;

            uint8_t* data = (uint8_t*) kmalloc(size);
            memcpy(data, (void*) mod->mod_start, size);

            if (!strcmp(module_name, "disk.img")) {
                vfs_node_t* root = vfs_mount("ext2", data, size);
                if (!(root && vfs_set_root(root))) {
                    kprintf_error("failed to mount ext2 from module %s", mod->name);
                }
            }
        }

        tag = (mb2_tag_t*) ((uintptr_t) tag + align_to(tag->size, 8));
    }
    set_font_scale(1);

    if (vfs_root()) {
        if (!proc_run_path("/bin/shell", NULL)) {
            kprintf("failed to run /bin/shell\n");
        }
    } else {
        kprintf_error("no root filesystem mounted; skipping /bin/shell");
    }

    proc_enter_usermode();
    infinite_loop();
}

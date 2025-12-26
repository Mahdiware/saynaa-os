export

## OS NAME
NAME = saynaa-os

CC         := clang
LD         := ld
ISO        := $(PWD)/$(NAME).iso
BASE       := $(PWD)/base
INCLUDE    := $(BASE)/include
BIN        := $(BASE)/bin
LIB        := $(BASE)/lib
AFLAGS     := -f elf32 -g
LDFLAGS    := -nostdlib -m elf_i386
BUILD      := build
OBJ_DIR    := $(BUILD)/obj/

MAKE       := $(MAKE) -s
ISODIR     := $(PWD)/isodir
KERNEL     := $(ISODIR)/boot/$(NAME).bin
IMAGE      := $(ISODIR)/modules/disk.img

PROJECTS   := kernel libraries userspace
PROJ_SETUP := $(PROJECTS:=.setup)
PROJ_CLEAN := $(PROJECTS:=.clean)

CFLAGS     := -std=gnu11 -ffreestanding -g -O0 \
              -I$(INCLUDE) \
              -target i386-pc-none-eabi -m32 \
              -mno-mmx -mno-sse -mno-sse2 \
              -Wno-ignored-attributes

.PHONY: all build qemu clean

all: build $(ISO)

build: initial $(PROJECTS)

kernel: libraries
userspace: libraries

# Copy headers before building anything
$(PROJECTS): $(PROJ_SETUP)
	@$(MAKE) -C $@ build

$(ISO): $(PROJECTS)
	@printf "[ building ISO... ]\n"
	@mkdir -p $(ISODIR)/boot/grub
	@grub-mkrescue -o $@ $(ISODIR) -V $(NAME)

qemu: all
	@qemu-system-i386 -cdrom $(ISO) -serial file:serial.log
	@cat serial.log

debug: $(ISO)
	@qemu-system-i386 -cdrom $(ISO) -S -s      &
	@gdb $(KERNEL)                             \
		-ex 'break kernel_main'                \
		-ex 'set architecture i386' 		   \
		-ex 'target remote :1234'              \

clean: $(PROJ_CLEAN)
	@rm -f $(ISO)
	@rm -rf $(BASE)
	@rm -rf $(ISODIR)

initial:
	@mkdir -p $(BASE)
	@mkdir -p $(INCLUDE)
	@mkdir -p $(ISODIR)/boot/grub

%.setup: %/
	@$(MAKE) -C $< setup target=$<

%.clean: %/
	@$(MAKE) -C $< clean

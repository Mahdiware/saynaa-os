include ../util/variables.mk

LIBS := -lgui -lc

C_SRCS := $(shell find src -type f -name '*.c')
A_SRCS := $(shell find src -type f -name '*.asm')

OBJS := $(addprefix $(OBJ_DIR), $(C_SRCS:.c=.o) $(A_SRCS:.asm=.o))
MODS := $(addprefix $(BIN)/, $(C_SRCS:src/%.c=%) $(A_SRCS:src/%.asm=%))
INIT := $(BUILD)/init.o

.PHONY: all build setup clean

$(IMAGE): $(MODS)
	@mkdir -p $(dir $@)
	@[ -d $(ASSETS)/default ] && cp -r $(ASSETS)/default/* $(BASE) || true
	@touch $(IMAGE)
	@dd if=/dev/zero of=$(IMAGE) bs=1024 count=9000 2> /dev/null
	@mkfs.ext2 $(IMAGE) -d $(BASE) > /dev/null 2>&1

$(MODS): $(BIN)/% : $(OBJ_DIR)src/%.o $(INIT)
	@mkdir -p $(dir $@)
	@$(LD) $(LDFLAGS) $< $(LIBS) -o $@
	@rm -f $(ISO)

build: $(IMAGE)

$(INIT): init.asm
	@nasm $(AFLAGS) $< -o $@

include ../util/targets.mk

setup: userspace_copy_headers

userspace_copy_headers:
	@if [ -d include ]; then \
		mkdir -p "$(INCLUDE)"; \
		cp -a include/. "$(INCLUDE)/"; \
	fi


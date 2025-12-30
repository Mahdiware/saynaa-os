include ../../util/variables.mk

LIBGUI    := $(LIB)/libgui.a

.PHONY: all build setup clean

$(LIBGUI): $(OBJS)
	@mkdir -p $(dir $@)
	@$(AR) rcs $@ $^
	@rm -f $(ISO)

build: $(LIBGUI)

include ../../util/targets.mk


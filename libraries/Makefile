LIBRARIES   := libc libgui
LIBR_SETUP 	:= $(LIBRARIES:=.setup)
LIBR_CLEAN 	:= $(LIBRARIES:=.clean)

$(LIBRARIES): $(LIBR_SETUP)
	@$(MAKE) -C $@ build

build: $(LIBRARIES)

%.setup: %/
	@$(MAKE) -C $< setup target=$<

%.clean: %/
	@$(MAKE) -C $< clean

setup: $(LIBR_SETUP)
clean: $(LIBR_CLEAN)
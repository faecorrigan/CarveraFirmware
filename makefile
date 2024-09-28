#!/usr/bin/make

DIRS = mbed mri src
DIRSCLEAN = $(addsuffix .clean,$(DIRS))

all:
	@ $(MAKE) -C mbed
	@ $(MAKE) -C mri arm
	@echo Building Smoothie
	@ $(MAKE) -C src

clean: $(DIRSCLEAN)

$(DIRSCLEAN): %.clean:
	@echo Cleaning $*
	@ $(MAKE) -C $*  clean

debug-store:
	@ $(MAKE) -C src debug-store

flash:
	@ $(MAKE) -C src flash

dfu:
	@ $(MAKE) -C src dfu

upload:
	@ $(MAKE) -C src upload

debug:
	@ $(MAKE) -C src debug

console:
	@ $(MAKE) -C src console

.PHONY: all $(DIRS) $(DIRSCLEAN) debug-store flash upload debug console dfu

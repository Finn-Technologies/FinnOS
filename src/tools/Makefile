# The host-side tools: things you run while working on the shell, never on
# the device.
#
#   vgtest     rasterises the icon set and writes a sheet, to see what the
#              vector rasteriser actually does with the assets
#   dockprev   renders the real dock code without a VM, a DRM device or a
#              compositor — the same drawing code, on the host

CC      ?= cc
CFLAGS  ?= -O1 -Wall -Wextra
CFLAGS  += -I../compositor -I../core

# The drawing primitives, minus the vector rasteriser: vgtest includes
# finnvg.c directly, so it must not also link it.
DRAW     = ../compositor/finntext.c ../compositor/finndraw.c \
           ../compositor/finnblur.c ../compositor/finnanim.c

FREETYPE_CFLAGS = $(shell pkg-config --cflags freetype2)
FREETYPE_LIBS   = $(shell pkg-config --libs freetype2)

all: vgtest dockprev

vgtest: vgtest.c ../compositor/finnvg.c ../compositor/finnvg.h
	$(CC) $(CFLAGS) -o $@ vgtest.c -lm

# The libraries follow the sources that need them: with --as-needed, a
# linker drops a library that appears before its first undefined symbol.
dockprev: dockprev.c ../compositor/finndock.c $(DRAW) ../compositor/finnvg.c
	$(CC) $(CFLAGS) $(FREETYPE_CFLAGS) -o $@ \
	    dockprev.c ../compositor/finndock.c $(DRAW) ../compositor/finnvg.c -lm $(FREETYPE_LIBS)

clean:
	rm -f vgtest dockprev

.PHONY: all clean

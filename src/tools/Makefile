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

FREETYPE = $(shell pkg-config --cflags freetype2) $(shell pkg-config --libs freetype2)

all: vgtest dockprev

vgtest: vgtest.c $(DRAW) ../compositor/finnvg.h
	$(CC) $(CFLAGS) $(FREETYPE) -o $@ vgtest.c $(DRAW) -lm

dockprev: dockprev.c ../compositor/finndock.c $(DRAW) ../compositor/finnvg.c
	$(CC) $(CFLAGS) $(FREETYPE) -o $@ \
	    dockprev.c ../compositor/finndock.c $(DRAW) ../compositor/finnvg.c -lm

clean:
	rm -f vgtest dockprev

.PHONY: all clean

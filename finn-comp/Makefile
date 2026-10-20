# finn-comp — FinnOS compositor, control plane and tooling
CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -std=gnu11
PKGS     = libdrm libwebp freetype2 alsa gbm egl glesv2
CFLAGS  += $(shell pkg-config --cflags $(PKGS))
LDLIBS  += $(shell pkg-config --libs $(PKGS)) -lm

CORE     = finnvg.c finntext.c finndraw.c finnblur.c finnanim.c finnstate.c \
           finnaudio.c finndock.c finncc.c finnclock.c finnmenu.c finnnotify.c \
           finnpointer.c finngpu.c finnapps.c finnsock.c

BINS     = finn-comp finnd finnctl

all: $(BINS)

HDRS     = finnvg.h finntext.h finndraw.h finnblur.h finnanim.h finnstate.h \
           finnaudio.h finndock.h finncc.h finnclock.h finnmenu.h finnnotify.h \
           finnpointer.h finngpu.h finnapps.h finnproto.h finnsock.h

finn-comp: finn-comp.c $(CORE) $(HDRS)
	$(CC) $(CFLAGS) -o $@ finn-comp.c $(CORE) $(LDLIBS)

finnd: finnd.c finnstate.c finnaudio.c finnapps.c finnsock.c finnstate.h finnaudio.h finnapps.h finnproto.h finnsock.h
	$(CC) $(CFLAGS) -o $@ finnd.c finnstate.c finnaudio.c finnapps.c finnsock.c $(LDLIBS)

finnctl: finnctl.c finnsock.c finnsock.h finnproto.h
	$(CC) $(CFLAGS) -o $@ finnctl.c finnsock.c

PREFIX  ?= /usr/local
BINDIR  ?= $(PREFIX)/bin
ICONDIR ?= /usr/share/finn/icons
DATADIR ?= /usr/share/finn

install: all
	install -Dm755 finn-comp $(DESTDIR)$(BINDIR)/finn-comp
	install -Dm755 finnd     $(DESTDIR)$(BINDIR)/finnd
	install -Dm755 finnctl   $(DESTDIR)$(BINDIR)/finnctl
	install -d $(DESTDIR)$(ICONDIR)
	install -m644 icons/*.svg $(DESTDIR)$(ICONDIR)/
	install -Dm644 ../assets/apps.registry $(DESTDIR)$(DATADIR)/apps.registry

# Host-side preview: renders the real dock code without a VM or DRM.
dockprev: dockprev.c finndock.c finndraw.c finnblur.c finntext.c finnanim.c finnvg.c
	cc -O1 -Wall $(shell pkg-config --cflags freetype2) -o $@ \
	   dockprev.c finndock.c finndraw.c finnblur.c finntext.c finnanim.c finnvg.c \
	   $(shell pkg-config --libs freetype2) -lm

clean:
	rm -f $(BINS) dockprev fvtest sheet.ppm sheet.png *.ppm *.png

.PHONY: all install clean

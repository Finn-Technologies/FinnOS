# finn-comp — the FinnOS compositor: the display server and the shell.
#
# One process owns the scanout buffer, the pointer and the shell chrome, and
# draws them itself: direct DRM/KMS, with a Wayland server for external app
# surfaces and no toolkit in the shell. The shell surfaces — the dock, the Control Center, the clock page
# and the Start Menu — are part of this process, not separate programs, so a
# surface costs a blit rather than a round trip.
#
# The parts:
#   finnvg       vector rasteriser for the icon set
#   finntext     Fixel Display text
#   finndraw     the shared drawing primitives
#   finnblur     the blurred wallpaper the frosted surfaces sample
#   finnanim     the animation tracks
#   finnmotion   the shell's motion, in one place
#   finnnotify   the shell's notification queue
#   finnpointer  evdev pointer + cursor sprite
#   finnwayland  xdg-shell clients, SHM surfaces and seat delivery
#   finngpu      GBM/EGL/GLES2 compositing, when the machine has it
#   finndock     the dock
#   finncontrol  the Control Center
#   finnclock    the clock page
#   finnmenu     the Start Menu
#   finnboot     the boot screen: the mark, and the progress of the wake-up
#   finn-comp    DRM/KMS, wallpaper, composition and page flips

CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -std=gnu11
CFLAGS  += -pthread
PKGS     = libdrm libwebp freetype2 alsa gbm egl glesv2 wayland-server xkbcommon
CFLAGS  += $(shell pkg-config --cflags $(PKGS)) -I../core
LDLIBS  += $(shell pkg-config --libs $(PKGS)) -lm
WAYLAND_PROTOCOLS_DIR ?= $(shell pkg-config --variable=pkgdatadir wayland-protocols)
XDG_SHELL_XML = $(WAYLAND_PROTOCOLS_DIR)/stable/xdg-shell/xdg-shell.xml
XDG_SHELL_GEN = xdg-shell-server-protocol.c xdg-shell-server-protocol.h
XDG_DEC_XML   = $(WAYLAND_PROTOCOLS_DIR)/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml
XDG_DEC_GEN   = xdg-decoration-unstable-v1-server-protocol.c xdg-decoration-unstable-v1-server-protocol.h

BROWSER_PKGS   = libpng libjpeg
BROWSER_CFLAGS = $(shell pkg-config --cflags $(BROWSER_PKGS)) -I../core
BROWSER_LDLIBS = $(shell pkg-config --libs $(BROWSER_PKGS)) -lm

CORE     = ../core/libfinncore.a

SHELL_   = finnvg.c finntext.c finndraw.c finnblur.c finnanim.c \
           finnnotify.c finnpointer.c finnkbd.c finngpu.c finndock.c \
           finncontrol.c finnclock.c finnmenu.c finnwindow.c finnsettingsapp.c \
           finnappui.c finnfilesapp.c finnstoreapp.c finnbrowserapp.c finnsetup.c finnboot.c finnwayland.c

HDRS     = finnvg.h finntext.h finndraw.h finnblur.h finnanim.h \
           finnmotion.h finnnotify.h finnpointer.h finnkbd.h finngpu.h \
           finndock.h finncontrol.h finnclock.h finnmenu.h finnwindow.h finnsettingsapp.h \
           finnappui.h finnfilesapp.h finnstoreapp.h finnbrowserapp.h finnsetup.h finnboot.h finnwayland.h ../core/finnapps.h ../core/finnconn.h \
           ../core/finnproto.h ../core/finnstate.h ../core/finnbrowser.h

all: finn-comp finn-browser

finn-comp: finn-comp.c $(SHELL_) $(CORE) $(HDRS) $(XDG_SHELL_GEN) $(XDG_DEC_GEN)
	$(CC) $(CFLAGS) -o $@ finn-comp.c $(SHELL_) xdg-shell-server-protocol.c xdg-decoration-unstable-v1-server-protocol.c $(CORE) $(LDLIBS)

finn-browser: finn-browser-engine.c $(CORE) ../core/finnbrowser.h ../core/finnproto.h ../core/finnsock.h
	$(CC) -O2 -Wall -Wextra -std=gnu11 $(BROWSER_CFLAGS) -o $@ finn-browser-engine.c $(CORE) $(BROWSER_LDLIBS)

xdg-shell-server-protocol.c: $(XDG_SHELL_XML)
	wayland-scanner private-code $< $@

xdg-shell-server-protocol.h: $(XDG_SHELL_XML)
	wayland-scanner server-header $< $@

xdg-decoration-unstable-v1-server-protocol.c: $(XDG_DEC_XML)
	wayland-scanner private-code $< $@

xdg-decoration-unstable-v1-server-protocol.h: $(XDG_DEC_XML)
	wayland-scanner server-header $< $@

PREFIX  ?= /usr/local
BINDIR  ?= $(PREFIX)/bin
ICONDIR ?= /usr/share/finn/icons
DATADIR ?= /usr/share/finn

install: finn-comp finn-browser
	install -Dm755 finn-comp $(DESTDIR)$(BINDIR)/finn-comp
	install -Dm755 finn-browser $(DESTDIR)$(BINDIR)/finn-browser
	install -d $(DESTDIR)$(ICONDIR)
	install -m644 ../../assets/icons/shell/*.svg ../../assets/icons/apps/*.svg $(DESTDIR)$(ICONDIR)/
	install -Dm644 ../../assets/apps.registry $(DESTDIR)$(DATADIR)/apps.registry

clean:
	rm -f finn-comp finn-browser $(XDG_SHELL_GEN) $(XDG_DEC_GEN)

.PHONY: all install clean

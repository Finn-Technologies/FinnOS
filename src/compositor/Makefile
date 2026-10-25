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

CORE     = ../core/libfinncore.a

SHELL_   = finnvg.c finntext.c finndraw.c finnblur.c finnanim.c \
           finnnotify.c finnpointer.c finnkbd.c finngpu.c finndock.c \
           finncontrol.c finnclock.c finnmenu.c finnwindow.c finnsettingsapp.c \
           finnappui.c finnfilesapp.c finnstoreapp.c finnsetup.c finnboot.c finnwayland.c

HDRS     = finnvg.h finntext.h finndraw.h finnblur.h finnanim.h \
           finnmotion.h finnnotify.h finnpointer.h finnkbd.h finngpu.h \
           finndock.h finncontrol.h finnclock.h finnmenu.h finnwindow.h finnsettingsapp.h \
           finnappui.h finnfilesapp.h finnstoreapp.h finnsetup.h finnboot.h finnwayland.h ../core/finnapps.h ../core/finnconn.h \
           ../core/finnproto.h ../core/finnstate.h

all: finn-comp

finn-comp: finn-comp.c $(SHELL_) $(CORE) $(HDRS) $(XDG_SHELL_GEN)
	$(CC) $(CFLAGS) -o $@ finn-comp.c $(SHELL_) xdg-shell-server-protocol.c $(CORE) $(LDLIBS)

xdg-shell-server-protocol.c: $(XDG_SHELL_XML)
	wayland-scanner private-code $< $@

xdg-shell-server-protocol.h: $(XDG_SHELL_XML)
	wayland-scanner server-header $< $@

PREFIX  ?= /usr/local
BINDIR  ?= $(PREFIX)/bin
ICONDIR ?= /usr/share/finn/icons
DATADIR ?= /usr/share/finn

install: finn-comp
	install -Dm755 finn-comp $(DESTDIR)$(BINDIR)/finn-comp
	install -d $(DESTDIR)$(ICONDIR)
	install -m644 ../../assets/icons/shell/*.svg ../../assets/icons/apps/*.svg $(DESTDIR)$(ICONDIR)/
	install -Dm644 ../../assets/apps.registry $(DESTDIR)$(DATADIR)/apps.registry

clean:
	rm -f finn-comp $(XDG_SHELL_GEN)

.PHONY: all install clean

# finn-comp — the FinnOS compositor: the display server and the shell.
#
# One process owns the scanout buffer, the pointer and the shell chrome, and
# draws them itself: direct DRM/KMS, no X11, no Wayland, no toolkit in the
# middle. The shell surfaces — the dock, the Control Center, the clock page
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
#   finngpu      GBM/EGL/GLES2 compositing, when the machine has it
#   finndock     the dock
#   finncontrol  the Control Center
#   finnclock    the clock page
#   finnmenu     the Start Menu
#   finnboot     the boot screen: the mark, and the progress of the wake-up
#   finn-comp    DRM/KMS, wallpaper, composition and page flips

CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -std=gnu11
PKGS     = libdrm libwebp freetype2 alsa gbm egl glesv2
CFLAGS  += $(shell pkg-config --cflags $(PKGS)) -I../core
LDLIBS  += $(shell pkg-config --libs $(PKGS)) -lm

CORE     = ../core/libfinncore.a

SHELL_   = finnvg.c finntext.c finndraw.c finnblur.c finnanim.c \
           finnnotify.c finnpointer.c finnkbd.c finngpu.c finndock.c \
           finncontrol.c finnclock.c finnmenu.c finnwindow.c finnsetup.c finnboot.c

HDRS     = finnvg.h finntext.h finndraw.h finnblur.h finnanim.h \
           finnmotion.h finnnotify.h finnpointer.h finnkbd.h finngpu.h \
           finndock.h finncontrol.h finnclock.h finnmenu.h finnwindow.h \
           finnsetup.h finnboot.h ../core/finnapps.h ../core/finnconn.h \
           ../core/finnproto.h ../core/finnstate.h

all: finn-comp

finn-comp: finn-comp.c $(SHELL_) $(CORE) $(HDRS)
	$(CC) $(CFLAGS) -o $@ finn-comp.c $(SHELL_) $(CORE) $(LDLIBS)

PREFIX  ?= /usr/local
BINDIR  ?= $(PREFIX)/bin
ICONDIR ?= /usr/share/finn/icons
DATADIR ?= /usr/share/finn

install: finn-comp
	install -Dm755 finn-comp $(DESTDIR)$(BINDIR)/finn-comp
	install -d $(DESTDIR)$(ICONDIR)
	install -m644 icons/*.svg $(DESTDIR)$(ICONDIR)/
	install -Dm644 ../../assets/apps.registry $(DESTDIR)$(DATADIR)/apps.registry

clean:
	rm -f finn-comp

.PHONY: all install clean

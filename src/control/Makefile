# finnd and finnctl — the FinnOS control plane and its client.
#
#   finnd     the control plane: the single permissioned entry point for
#             state, input, display, power and apps
#   finnctl   the client: every verb is scriptable from a shell
#
# The two are the same source tree because they are the same protocol from
# two ends, and the protocol is one header (core/finnproto.h).

CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -std=gnu11
CFLAGS  += -I../core
PKGS     = alsa
LDLIBS  += $(shell pkg-config --libs $(PKGS))

CORE     = ../core/libfinncore.a

HDRS     = ../core/finnapps.h ../core/finnconn.h ../core/finnproto.h \
           ../core/finnsock.h ../core/finnstate.h finncompat.h

all: finnd finnctl finn-window

finnd: finnd.c finncompat.c $(CORE) $(HDRS)
	$(CC) $(CFLAGS) -o $@ finnd.c finncompat.c $(CORE) $(LDLIBS)

finnctl: finnctl.c $(CORE) $(HDRS)
	$(CC) $(CFLAGS) -o $@ finnctl.c $(CORE) $(LDLIBS)

finn-window: finn-window.c $(CORE) $(HDRS)
	$(CC) $(CFLAGS) -o $@ finn-window.c $(CORE)

PREFIX  ?= /usr/local
BINDIR  ?= $(PREFIX)/bin

install: all
	install -Dm755 finnd       $(DESTDIR)$(BINDIR)/finnd
	install -Dm755 finnctl     $(DESTDIR)$(BINDIR)/finnctl
	install -Dm755 finn-window $(DESTDIR)$(BINDIR)/finn-window
	ln -sf finn-window $(DESTDIR)$(BINDIR)/finn-settings
	ln -sf finn-window $(DESTDIR)$(BINDIR)/finn-files
	ln -sf finn-window $(DESTDIR)$(BINDIR)/finn-store
	ln -sf finn-window $(DESTDIR)$(BINDIR)/arc

clean:
	rm -f finnd finnctl finn-window

.PHONY: all install clean

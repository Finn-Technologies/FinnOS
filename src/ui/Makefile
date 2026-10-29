PKGS     = wayland-client xkbcommon freetype2
CC       ?= cc
CXX      ?= c++
CFLAGS   ?= -O2 -g -Wall -Wextra -Wpedantic -Werror -std=c11
CXXFLAGS ?= -O2 -g -Wall -Wextra -Wpedantic -Werror -std=c++20
CFLAGS   += -I. -Iraster -Ibuild $(shell pkg-config --cflags $(PKGS))
CXXFLAGS += -I. -Iraster -Ibuild $(shell pkg-config --cflags $(PKGS))
LDLIBS   += $(shell pkg-config --libs $(PKGS)) -lm

BUILD = build
GEN   = $(BUILD)/xdg-shell-client-protocol.h $(BUILD)/xdg-shell-protocol.c \
        $(BUILD)/wlr-layer-shell-unstable-v1-client-protocol.h \
        $(BUILD)/wlr-layer-shell-unstable-v1-protocol.c
OBJS  = $(BUILD)/canvas.o $(BUILD)/app.o $(BUILD)/theme.o \
        $(BUILD)/finnvg.o $(BUILD)/finntext.o \
        $(BUILD)/xdg-shell-protocol.o $(BUILD)/wlr-layer-shell-protocol.o

all: libfinnui.a

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/xdg-shell-client-protocol.h: protocol/xdg-shell.xml | $(BUILD)
	wayland-scanner client-header $< $@

$(BUILD)/xdg-shell-protocol.c: protocol/xdg-shell.xml | $(BUILD)
	wayland-scanner private-code $< $@

$(BUILD)/wlr-layer-shell-unstable-v1-client-protocol.h: protocol/wlr-layer-shell-unstable-v1.xml | $(BUILD)
	wayland-scanner client-header $< $@

$(BUILD)/wlr-layer-shell-unstable-v1-protocol.c: protocol/wlr-layer-shell-unstable-v1.xml | $(BUILD)
	wayland-scanner private-code $< $@

$(BUILD)/canvas.o: canvas.cpp finn/ui.hpp $(BUILD)/xdg-shell-client-protocol.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/app.o: app.cpp finn/ui.hpp $(GEN) | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/theme.o: theme.cpp finn/ui.hpp generated_tokens.hpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/finnvg.o: raster/finnvg.c raster/finnvg.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/finntext.o: raster/finntext.c raster/finntext.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/xdg-shell-protocol.o: $(BUILD)/xdg-shell-protocol.c $(BUILD)/xdg-shell-client-protocol.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/wlr-layer-shell-protocol.o: $(BUILD)/wlr-layer-shell-unstable-v1-protocol.c $(BUILD)/wlr-layer-shell-unstable-v1-client-protocol.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

libfinnui.a: $(OBJS)
	ar rcs $@ $(OBJS)

PREFIX ?= /usr/local
INCDIR ?= $(PREFIX)/include
LIBDIR ?= $(PREFIX)/lib

install: libfinnui.a
	install -d $(DESTDIR)$(INCDIR)/finn
	install -m644 finn/ui.hpp $(DESTDIR)$(INCDIR)/finn/ui.hpp
	install -d $(DESTDIR)$(LIBDIR)
	install -m644 libfinnui.a $(DESTDIR)$(LIBDIR)/libfinnui.a
	install -d $(DESTDIR)$(LIBDIR)/pkgconfig
	install -m644 finnui.pc $(DESTDIR)$(LIBDIR)/pkgconfig/finnui.pc

clean:
	rm -rf $(BUILD) libfinnui.a

.PHONY: all install clean

PKGS     = wlroots-0.20 wayland-server xkbcommon pixman-1 xcb libsystemd
CC       ?= cc
CXX      ?= c++
CFLAGS   ?= -O2 -g -Wall -Wextra -Werror -DWLR_USE_UNSTABLE
CXXFLAGS ?= -O2 -g -Wall -Wextra -Wpedantic -Werror -std=c++20 -DWLR_USE_UNSTABLE
CPPFLAGS += -I.. -Ibuild $(shell pkg-config --cflags $(PKGS))
LDLIBS   += $(shell pkg-config --libs $(PKGS))

WAYLAND_SCANNER = $(shell pkg-config --variable=wayland_scanner wayland-scanner 2>/dev/null || command -v wayland-scanner)
WL_PROTO_DIR    = $(shell pkg-config --variable=pkgdatadir wayland-protocols)

GEN_HDRS = build/xdg-shell-protocol.h \
           build/wlr-layer-shell-unstable-v1-protocol.h \
           build/cursor-shape-v1-protocol.h

CXX_SRCS = main.cpp server.cpp window.cpp layer.cpp
C_SRCS   = xwayland_shim.c
OBJS     = $(CXX_SRCS:.cpp=.o) $(C_SRCS:.c=.o)
HDRS     = server.hpp internal.hpp wlroots.hpp ../common/paths.hpp $(GEN_HDRS)
BIN      = finn-comp

all: $(BIN)

build/xdg-shell-protocol.h: ../protocol/xdg-shell.xml
	@mkdir -p build
	$(WAYLAND_SCANNER) server-header $< $@

build/wlr-layer-shell-unstable-v1-protocol.h: ../protocol/wlr-layer-shell-unstable-v1.xml
	@mkdir -p build
	$(WAYLAND_SCANNER) server-header $< $@

build/cursor-shape-v1-protocol.h: $(WL_PROTO_DIR)/staging/cursor-shape/cursor-shape-v1.xml
	@mkdir -p build
	$(WAYLAND_SCANNER) server-header $< $@

%.o: %.cpp $(HDRS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BIN): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS) $(LDLIBS)

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

install: $(BIN)
	install -Dm755 $(BIN) $(DESTDIR)$(BINDIR)/$(BIN)

clean:
	rm -rf $(BIN) $(OBJS) build

.PHONY: all install clean

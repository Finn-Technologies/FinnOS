PKGS     = wlroots-0.20 wayland-server xkbcommon libsystemd
CXX      ?= c++
CXXFLAGS ?= -std=c++20 -O2 -g -Wall -Wextra -Wpedantic -Werror
CXXFLAGS += -I.. -Ibuild -DWLR_USE_UNSTABLE $(shell pkg-config --cflags $(PKGS))
LDLIBS   += $(shell pkg-config --libs $(PKGS)) -lm

LAYER_XML = ../ui/protocol/wlr-layer-shell-unstable-v1.xml
LAYER_HDR = build/wlr-layer-shell-unstable-v1-protocol.h

SRCS = main.cpp server.cpp
HDRS = server.hpp wlroots.hpp
BIN  = finn-comp

PREFIX  ?= /usr/local
BINDIR  ?= $(PREFIX)/bin

all: $(BIN)

build:
	mkdir -p build

$(LAYER_HDR): $(LAYER_XML) | build
	wayland-scanner server-header $< $@

$(BIN): $(SRCS) $(HDRS) $(LAYER_HDR)
	$(CXX) $(CXXFLAGS) -o $@ $(SRCS) $(LDLIBS)

install: $(BIN)
	install -Dm755 $(BIN) $(DESTDIR)$(BINDIR)/$(BIN)

clean:
	rm -rf $(BIN) build

.PHONY: all install clean

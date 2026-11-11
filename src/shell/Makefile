PKGS     = wayland-client xkbcommon freetype2 libwebp
CXX      ?= c++
CXXFLAGS ?= -O2 -g -Wall -Wextra -Wpedantic -Werror -std=c++20
CXXFLAGS += -I../../src -I../../src/ui
LDLIBS   += ../../src/ui/libfinnui.a $(shell pkg-config --libs $(PKGS)) -lm

SRCS = main.cpp search.cpp ../../src/session/client.cpp
BIN  = finn-shell

all: $(BIN)

$(BIN): $(SRCS) search.hpp ../../src/ui/libfinnui.a ../../src/session/client.hpp
	$(CXX) $(CXXFLAGS) -o $@ $(SRCS) $(LDLIBS)

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

install: $(BIN)
	install -Dm755 $(BIN) $(DESTDIR)$(BINDIR)/$(BIN)

clean:
	rm -f $(BIN)

.PHONY: all install clean

PKGS     = wayland-client xkbcommon freetype2 libwebp
CXX      ?= c++
CXXFLAGS ?= -O2 -g -Wall -Wextra -Wpedantic -Werror -std=c++20
CXXFLAGS += -I../../src/ui
LDLIBS   += ../../src/ui/libfinnui.a $(shell pkg-config --libs $(PKGS)) -lm

BIN = finn-files

all: $(BIN)

$(BIN): main.cpp ../../src/ui/libfinnui.a
	$(CXX) $(CXXFLAGS) -o $@ main.cpp $(LDLIBS)

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

install: $(BIN)
	install -Dm755 $(BIN) $(DESTDIR)$(BINDIR)/$(BIN)

clean:
	rm -f $(BIN)

.PHONY: all install clean

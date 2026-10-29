PKGS     = wlroots-0.20 wayland-server xkbcommon
CXX      ?= c++
CXXFLAGS ?= -std=c++20 -O2 -g -Wall -Wextra -Wpedantic -Werror
CXXFLAGS += -I.. -DWLR_USE_UNSTABLE $(shell pkg-config --cflags $(PKGS))
LDLIBS   += $(shell pkg-config --libs $(PKGS)) -lm

SRCS = main.cpp server.cpp
HDRS = server.hpp wlroots.hpp
BIN  = finn-comp

PREFIX  ?= /usr/local
BINDIR  ?= $(PREFIX)/bin

all: $(BIN)

$(BIN): $(SRCS) $(HDRS)
	$(CXX) $(CXXFLAGS) -o $@ $(SRCS) $(LDLIBS)

install: $(BIN)
	install -Dm755 $(BIN) $(DESTDIR)$(BINDIR)/$(BIN)

clean:
	rm -f $(BIN)

.PHONY: all install clean

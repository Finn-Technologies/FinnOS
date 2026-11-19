CXX      ?= c++
CC       ?= cc
CXXFLAGS ?= -O2 -g -Wall -Wextra -Wpedantic -Werror -std=c++20
CXXFLAGS += -I..
LDLIBS   += -lz -lpthread -ldl

CXX_SRCS = main.cpp plist.cpp icns.cpp installer.cpp \
           objc_runtime.cpp libsystem.cpp foundation.cpp \
           coregraphics.cpp swift_runtime.cpp \
           appkit_bridge.cpp macho_loader.cpp \
           ../compat/desktop_entry.cpp
ASM_SRCS = objc_msgsend.S
HDRS     = macho_format.hpp plist.hpp icns.hpp installer.hpp \
           objc_runtime.hpp libsystem.hpp foundation.hpp \
           coregraphics.hpp swift_runtime.hpp \
           appkit_bridge.hpp macho_loader.hpp
BIN      = finn-darwin

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

all: $(BIN)

objc_msgsend.o: objc_msgsend.S
	$(CC) -c -o $@ $<

$(BIN): $(CXX_SRCS) objc_msgsend.o $(HDRS)
	$(CXX) $(CXXFLAGS) -o $@ $(CXX_SRCS) objc_msgsend.o $(LDLIBS)

install: $(BIN)
	install -Dm755 $(BIN) $(DESTDIR)$(BINDIR)/$(BIN)

clean:
	rm -f $(BIN) objc_msgsend.o

.PHONY: all install clean

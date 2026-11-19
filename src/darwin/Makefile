CXX      ?= c++
CC       ?= cc
CXXFLAGS ?= -O2 -g -Wall -Wextra -Wpedantic -Werror -std=c++20
CXXFLAGS += -I..
LDLIBS   += -lz -lpthread -ldl

CXX_SRCS = main.cpp plist.cpp framework_coverage.cpp icns.cpp installer.cpp \
           objc_runtime.cpp libsystem.cpp foundation.cpp \
           coregraphics.cpp swift_runtime.cpp compat_symbols.cpp \
           appkit_bridge.cpp macho_loader.cpp carbon_compat.cpp \
           swiftui/abi_metadata.cpp swiftui/swift_runtime_ext.cpp \
           swiftui/views_and_scene.cpp swiftui/state_and_combine.cpp \
           swiftui/registry.cpp \
           ../compat/desktop_entry.cpp
ASM_SRCS = objc_msgsend.S
HDRS     = macho_format.hpp plist.hpp framework_coverage.hpp icns.hpp installer.hpp \
           objc_runtime.hpp libsystem.hpp foundation.hpp \
           coregraphics.hpp swift_runtime.hpp compat_symbols.hpp \
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

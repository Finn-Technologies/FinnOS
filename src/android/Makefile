CXX      ?= c++
CXXFLAGS ?= -O2 -g -Wall -Wextra -Wpedantic -Werror -std=c++20
CXXFLAGS += -I..
LDFLAGS  += -rdynamic
LDLIBS   += -lz -lpthread -ldl

CXX_SRCS = main.cpp zip_reader.cpp android_format.cpp apk_format.cpp dex_runtime.cpp \
           art_bridge.cpp ui_bridge.cpp android_installer.cpp
HDRS     = zip_reader.hpp android_format.hpp apk_format.hpp dex_runtime.hpp art_bridge.hpp \
           ui_bridge.hpp android_installer.hpp
BIN      = finn-android

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

all: $(BIN)

$(BIN): $(CXX_SRCS) $(HDRS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $(CXX_SRCS) $(LDLIBS)

install: $(BIN)
	install -Dm755 $(BIN) $(DESTDIR)$(BINDIR)/$(BIN)

clean:
	rm -f $(BIN)

.PHONY: all install clean

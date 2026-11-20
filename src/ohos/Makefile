CXX      ?= c++
CXXFLAGS ?= -O2 -g -Wall -Wextra -Wpedantic -Werror -std=c++20
CXXFLAGS += -I..
LDFLAGS  += -rdynamic
LDLIBS   += -lz -lpthread -ldl

CXX_SRCS = main.cpp zip_reader.cpp arkui_bridge.cpp arkui_declarative.cpp \
           abc_runtime.cpp ohos_installer.cpp
HDRS     = ohos_format.hpp zip_reader.hpp arkui_bridge.hpp \
           abc_runtime.hpp ohos_installer.hpp
BIN      = finn-ohos

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

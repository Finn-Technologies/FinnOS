PKGS     =
CXX      ?= c++
CXXFLAGS ?= -O2 -g -Wall -Wextra -Wpedantic -Werror -std=c++20
CXXFLAGS += -I..
LDLIBS   +=

SRCS = main.cpp client.cpp app_registry.cpp \
       ../compat/desktop_entry.cpp ../compat/flatpak_installer.cpp \
       ../compat/macos_runner.cpp ../compat/wine_runner.cpp ../system/sys_files.cpp
HDRS = client.hpp app_registry.hpp ../common/paths.hpp \
       ../compat/desktop_entry.hpp ../compat/flatpak_installer.hpp \
       ../compat/macos_runner.hpp ../compat/wine_runner.hpp ../system/sys_files.hpp
BIN  = finn-session

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

all: $(BIN)

$(BIN): $(SRCS) $(HDRS)
	$(CXX) $(CXXFLAGS) -o $@ $(SRCS) $(LDLIBS)

install: $(BIN)
	install -Dm755 $(BIN) $(DESTDIR)$(BINDIR)/$(BIN)

clean:
	rm -f $(BIN)

.PHONY: all install clean

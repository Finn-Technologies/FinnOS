PKGS     =
CXX      ?= c++
CXXFLAGS ?= -O2 -g -Wall -Wextra -Wpedantic -Werror -std=c++20
CXXFLAGS += -I..
LDLIBS   +=

SRCS = main.cpp client.cpp app_registry.cpp \
       ../compat/android_runner.cpp ../compat/appimage_runner.cpp ../compat/desktop_entry.cpp \
       ../compat/finnapp_installer.cpp ../compat/flatpak_installer.cpp ../compat/linux_app_probe.cpp \
       ../compat/linux_sandbox.cpp \
       ../compat/macos_runner.cpp ../compat/ohos_runner.cpp ../compat/pe_icon.cpp \
       ../compat/wine_runner.cpp ../compat/wine_prefix.cpp \
       ../compat/windows_registry.cpp \
       ../compat/msi_installer.cpp ../system/sys_files.cpp
HDRS = client.hpp app_registry.hpp ../common/paths.hpp \
       ../compat/android_runner.hpp ../compat/appimage_runner.hpp ../compat/elf_arch.hpp \
       ../compat/desktop_entry.hpp ../compat/finnapp_installer.hpp ../compat/flatpak_installer.hpp \
       ../compat/linux_app_probe.hpp ../compat/linux_sandbox.hpp \
       ../compat/macos_runner.hpp ../compat/ohos_runner.hpp ../compat/pe_icon.hpp \
       ../compat/wine_runner.hpp ../compat/wine_prefix.hpp \
       ../compat/windows_registry.hpp \
       ../compat/msi_installer.hpp ../system/sys_files.hpp
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

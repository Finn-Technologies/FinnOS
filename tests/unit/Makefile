CC       ?= cc
CXX      ?= c++
CXXFLAGS ?= -std=c++20 -O2 -g -Wall -Wextra -Wpedantic -Werror
SAN      ?= -fsanitize=address,undefined -fno-omit-frame-pointer

REGISTRY_SRCS = ../../src/session/app_registry.cpp \
                ../../src/compat/desktop_entry.cpp \
                ../../src/compat/macos_runner.cpp \
                ../../src/compat/ohos_runner.cpp \
                ../../src/compat/wine_runner.cpp \
                ../../src/system/sys_files.cpp
DARWIN_SRCS   = ../../src/darwin/plist.cpp \
                ../../src/darwin/icns.cpp \
                ../../src/darwin/installer.cpp \
                ../../src/darwin/objc_runtime.cpp \
                ../../src/darwin/libsystem.cpp \
                ../../src/darwin/foundation.cpp \
                ../../src/darwin/coregraphics.cpp \
                ../../src/darwin/swift_runtime.cpp \
                ../../src/darwin/appkit_bridge.cpp \
                ../../src/darwin/macho_loader.cpp \
                ../../src/compat/desktop_entry.cpp
OHOS_SRCS     = ../../src/ohos/zip_reader.cpp \
                ../../src/ohos/arkui_bridge.cpp \
                ../../src/ohos/abc_runtime.cpp \
                ../../src/ohos/ohos_installer.cpp \
                ../../src/compat/ohos_runner.cpp \
                ../../src/compat/desktop_entry.cpp
BINS          = test_app_registry test_animation test_spring test_session_client \
                test_search test_compat test_darwin test_ohos test_sys_files

all: $(BINS)

test_app_registry: test_app_registry.cpp $(REGISTRY_SRCS) \
		../../src/session/app_registry.hpp
	$(CXX) $(CXXFLAGS) $(SAN) -I../../src -o $@ test_app_registry.cpp $(REGISTRY_SRCS)

test_animation: test_animation.cpp ../../src/ui/finn/animation.hpp \
		../../src/common/paths.hpp
	$(CXX) $(CXXFLAGS) $(SAN) -I../../src/ui -o $@ test_animation.cpp

test_spring: test_spring.cpp ../../src/ui/finn/spring.hpp \
		../../src/ui/finn/animation.hpp ../../src/common/paths.hpp
	$(CXX) $(CXXFLAGS) $(SAN) -I../../src/ui -o $@ test_spring.cpp

test_session_client: test_session_client.cpp ../../src/session/client.cpp \
		../../src/session/client.hpp ../../src/common/paths.hpp
	$(CXX) $(CXXFLAGS) $(SAN) -I../../src -o $@ test_session_client.cpp \
		../../src/session/client.cpp

test_search: test_search.cpp ../../src/shell/search.cpp \
		../../src/shell/search.hpp ../../src/session/client.hpp
	$(CXX) $(CXXFLAGS) $(SAN) -I../../src -o $@ test_search.cpp \
		../../src/shell/search.cpp

test_compat: test_compat.cpp ../../src/compat/desktop_entry.cpp \
		../../src/compat/flatpak_installer.cpp \
		../../src/compat/macos_runner.cpp \
		../../src/compat/wine_runner.cpp ../../src/compat/desktop_entry.hpp \
		../../src/compat/flatpak_installer.hpp \
		../../src/compat/macos_runner.hpp ../../src/compat/wine_runner.hpp
	$(CXX) $(CXXFLAGS) $(SAN) -I../../src -o $@ test_compat.cpp \
		../../src/compat/desktop_entry.cpp \
		../../src/compat/flatpak_installer.cpp \
		../../src/compat/macos_runner.cpp \
		../../src/compat/wine_runner.cpp

test_darwin_msgsend.o: ../../src/darwin/objc_msgsend.S
	$(CC) -c -o $@ $<

test_darwin: test_darwin.cpp $(DARWIN_SRCS) test_darwin_msgsend.o
	$(CXX) $(CXXFLAGS) $(SAN) -I../../src -o $@ test_darwin.cpp \
		$(DARWIN_SRCS) test_darwin_msgsend.o -lz -lpthread -ldl

test_ohos: test_ohos.cpp $(OHOS_SRCS)
	$(MAKE) -C ../../src/ohos
	$(CXX) $(CXXFLAGS) $(SAN) -I../../src -o $@ test_ohos.cpp \
		$(OHOS_SRCS) -lz -lpthread -ldl

test_sys_files: test_sys_files.cpp ../../src/system/sys_files.cpp \
		../../src/system/sys_files.hpp ../../src/common/paths.hpp
	$(CXX) $(CXXFLAGS) $(SAN) -I../../src -o $@ test_sys_files.cpp \
		../../src/system/sys_files.cpp

run: $(BINS)
	./test_app_registry
	./test_animation
	./test_spring
	./test_session_client
	./test_search
	./test_compat
	./test_darwin
	./test_ohos
	./test_sys_files
	python3 test_tokens.py

clean:
	rm -f $(BINS) test_darwin_msgsend.o

.PHONY: all run clean

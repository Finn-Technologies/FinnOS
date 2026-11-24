CC       ?= cc
CXX      ?= c++
CXXFLAGS ?= -std=c++20 -O2 -g -Wall -Wextra -Wpedantic -Werror
SAN      ?= -fsanitize=address,undefined -fno-omit-frame-pointer

REGISTRY_SRCS = ../../src/session/app_registry.cpp \
                ../../src/compat/android_runner.cpp \
                ../../src/compat/appimage_runner.cpp \
                ../../src/compat/desktop_entry.cpp \
                ../../src/compat/linux_sandbox.cpp \
                ../../src/compat/macos_runner.cpp \
                ../../src/compat/ohos_runner.cpp \
                ../../src/compat/pe_icon.cpp \
                ../../src/compat/wine_runner.cpp \
                ../../src/compat/wine_prefix.cpp \
                ../../src/compat/msi_installer.cpp \
                ../../src/compat/windows_registry.cpp \
                ../../src/system/sys_files.cpp
DARWIN_SRCS   = ../../src/darwin/plist.cpp \
                ../../src/darwin/framework_coverage.cpp \
                ../../src/darwin/icns.cpp \
                ../../src/darwin/installer.cpp \
                ../../src/darwin/objc_runtime.cpp \
                ../../src/darwin/libsystem.cpp \
                ../../src/darwin/foundation.cpp \
                ../../src/darwin/coregraphics.cpp \
                ../../src/darwin/swift_runtime.cpp \
                ../../src/darwin/compat_symbols.cpp \
                ../../src/darwin/appkit_bridge.cpp \
                ../../src/darwin/carbon_compat.cpp \
                ../../src/darwin/macho_loader.cpp \
                ../../src/darwin/swiftui/abi_metadata.cpp \
                ../../src/darwin/swiftui/swift_runtime_ext.cpp \
                ../../src/darwin/swiftui/views_and_scene.cpp \
                ../../src/darwin/swiftui/state_and_combine.cpp \
                ../../src/darwin/swiftui/registry.cpp \
                ../../src/darwin/swiftui/foundation_overlay.cpp \
                ../../src/darwin/swiftui/render_graph.cpp \
                ../../src/darwin/swiftui/stdlib_metadata.cpp \
                ../../src/darwin/late_frameworks.cpp \
                ../../src/darwin/workspace_shims.cpp \
                ../../src/compat/desktop_entry.cpp
OHOS_SRCS     = ../../src/ohos/zip_reader.cpp \
                ../../src/ohos/arkui_bridge.cpp \
                ../../src/ohos/arkui_declarative.cpp \
                ../../src/ohos/arkui_bytecode_ui.cpp \
                ../../src/ohos/abc_runtime.cpp \
                ../../src/ohos/ohos_installer.cpp \
                ../../src/compat/ohos_runner.cpp \
                ../../src/compat/desktop_entry.cpp
ANDROID_SRCS  = ../../src/android/zip_reader.cpp ../../src/android/android_format.cpp \
                ../../src/android/apk_format.cpp ../../src/android/dex_runtime.cpp \
                ../../src/android/art_bridge.cpp ../../src/android/ui_bridge.cpp \
                ../../src/android/android_installer.cpp \
                ../../src/compat/android_runner.cpp \
                ../../src/compat/desktop_entry.cpp
BINS          = test_app_registry test_animation test_spring test_compositor \
                test_session_client test_search test_compat test_windows_modules \
                test_darwin test_ohos test_android test_sys_files test_oobe \
                test_recovery test_bootloader test_store

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

test_compositor: test_compositor.cpp ../../src/comp/core/workspace_model.cpp \
		../../src/comp/core/workspace_model.hpp \
		../../src/comp/core/input_router.cpp \
		../../src/comp/core/input_router.hpp \
		../../src/comp/core/scene_stack.hpp \
		../../src/ui/finn/form_factor.hpp \
		../../src/ui/generated_tokens.hpp
	$(CXX) $(CXXFLAGS) $(SAN) -I../../src -o $@ test_compositor.cpp \
		../../src/comp/core/workspace_model.cpp \
		../../src/comp/core/input_router.cpp

test_session_client: test_session_client.cpp ../../src/session/client.cpp \
		../../src/session/client.hpp ../../src/common/paths.hpp
	$(CXX) $(CXXFLAGS) $(SAN) -I../../src -o $@ test_session_client.cpp \
		../../src/session/client.cpp

test_search: test_search.cpp ../../src/shell/search.cpp \
		../../src/shell/search.hpp ../../src/session/client.hpp
	$(CXX) $(CXXFLAGS) $(SAN) -I../../src -o $@ test_search.cpp \
		../../src/shell/search.cpp

WINDOWS_SRCS = ../../src/compat/wine_prefix.cpp \
               ../../src/compat/msi_installer.cpp \
               ../../src/compat/windows_registry.cpp \
               ../../src/compat/wine_runner.cpp \
               ../../src/compat/desktop_entry.cpp

COMPAT_SRCS = ../../src/compat/appimage_runner.cpp \
              ../../src/compat/desktop_entry.cpp \
              ../../src/compat/finnapp_installer.cpp \
              ../../src/compat/flatpak_installer.cpp \
              ../../src/compat/linux_app_probe.cpp \
              ../../src/compat/linux_sandbox.cpp \
              ../../src/compat/macos_runner.cpp \
              ../../src/compat/pe_icon.cpp \
              ../../src/compat/wine_runner.cpp \
              ../../src/compat/wine_prefix.cpp \
              ../../src/compat/msi_installer.cpp \
              ../../src/compat/windows_registry.cpp

test_compat: test_compat.cpp $(COMPAT_SRCS) \
		../../src/compat/appimage_runner.hpp \
		../../src/compat/elf_arch.hpp \
		../../src/compat/desktop_entry.hpp \
		../../src/compat/flatpak_installer.hpp \
		../../src/compat/linux_app_probe.hpp \
		../../src/compat/linux_sandbox.hpp \
		../../src/compat/macos_runner.hpp ../../src/compat/wine_runner.hpp \
		../../src/compat/wine_prefix.hpp ../../src/compat/msi_installer.hpp \
		../../src/compat/windows_registry.hpp
	$(CXX) $(CXXFLAGS) $(SAN) -I../../src -o $@ test_compat.cpp $(COMPAT_SRCS)

test_windows_modules: test_windows_modules.cpp $(WINDOWS_SRCS) \
		../../src/compat/wine_prefix.hpp ../../src/compat/msi_installer.hpp \
		../../src/compat/windows_registry.hpp ../../src/compat/wine_runner.hpp
	$(CXX) $(CXXFLAGS) $(SAN) -I../../src -o $@ test_windows_modules.cpp $(WINDOWS_SRCS)

test_darwin_msgsend.o: ../../src/darwin/objc_msgsend.S
	$(CC) -c -o $@ $<

test_darwin: test_darwin.cpp $(DARWIN_SRCS) test_darwin_msgsend.o
	$(CXX) $(CXXFLAGS) $(SAN) -I../../src -o $@ test_darwin.cpp \
		$(DARWIN_SRCS) test_darwin_msgsend.o -lz -lpthread -ldl

test_ohos: test_ohos.cpp $(OHOS_SRCS)
	$(MAKE) -C ../../src/ohos
	$(CXX) $(CXXFLAGS) $(SAN) -I../../src -o $@ test_ohos.cpp \
		$(OHOS_SRCS) -lz -lpthread -ldl

test_android: test_android.cpp $(ANDROID_SRCS)
	$(CXX) $(CXXFLAGS) $(SAN) -I../../src -o $@ test_android.cpp \
		$(ANDROID_SRCS) -lz -lpthread -ldl

test_sys_files: test_sys_files.cpp ../../src/system/sys_files.cpp \
		../../src/system/sys_files.hpp ../../src/system/guest_tools.cpp \
		../../src/system/guest_tools.hpp ../../src/common/paths.hpp
	$(CXX) $(CXXFLAGS) $(SAN) -I../../src -o $@ test_sys_files.cpp \
		../../src/system/sys_files.cpp ../../src/system/guest_tools.cpp

test_oobe: test_oobe.cpp ../../src/shell/full_oobe_flow.cpp \
		../../src/shell/full_oobe_flow.hpp ../../src/common/paths.hpp
	$(CXX) $(CXXFLAGS) $(SAN) -I../../src -o $@ test_oobe.cpp \
		../../src/shell/full_oobe_flow.cpp

test_recovery: test_recovery.cpp ../../src/recovery/recovery_engine.cpp \
		../../src/recovery/recovery_engine.hpp
	$(CXX) $(CXXFLAGS) $(SAN) -I../../src -o $@ test_recovery.cpp \
		../../src/recovery/recovery_engine.cpp

test_bootloader: test_bootloader.cpp ../../src/boot/bls_parser.c \
		../../src/boot/boot_ui.c ../../src/boot/bls_parser.h ../../src/boot/boot_ui.h
	$(CC) -std=c11 -O2 -g -Wall -Wextra -Werror $(SAN) -c -o bls_parser.o ../../src/boot/bls_parser.c
	$(CC) -std=c11 -O2 -g -Wall -Wextra -Werror $(SAN) -c -o boot_ui.o ../../src/boot/boot_ui.c
	$(CXX) $(CXXFLAGS) $(SAN) -I../../src -o $@ test_bootloader.cpp bls_parser.o boot_ui.o

STORE_SRCS = ../../apps/store/providers/protobuf_wire.cpp \
             ../../apps/store/providers/aurora_play_provider.cpp \
             ../../apps/store/providers/flathub_provider.cpp \
             ../../apps/store/providers/winget_provider.cpp \
             ../../apps/store/providers/curated_catalog.cpp

test_store: test_store.cpp $(STORE_SRCS)
	$(CXX) $(CXXFLAGS) $(SAN) -I../../apps/store -I../../src -o $@ test_store.cpp $(STORE_SRCS)

run: $(BINS)
	./test_app_registry
	./test_animation
	./test_spring
	./test_compositor
	./test_session_client
	./test_search
	./test_compat
	./test_windows_modules
	./test_darwin
	./test_ohos
	./test_android
	./test_sys_files
	./test_oobe
	./test_recovery
	./test_bootloader
	./test_store
	python3 test_tokens.py

clean:
	rm -f $(BINS) test_darwin_msgsend.o

.PHONY: all run clean

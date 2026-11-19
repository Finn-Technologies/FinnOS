BUILD_DIR ?= build
PREFIX    ?= /usr/local
DESTDIR   ?=
VM        ?= finn@10.211.55.6
CMAKE_FLAGS ?= -DCMAKE_BUILD_TYPE=Release -DQT_NO_PRIVATE_MODULE_WARNING=ON

.PHONY: all configure ui qtplugin compositor session darwin ohos android shell apps tools \
        test test-unit check lint install clean deploy help

all: compositor session configure
	cmake --build $(BUILD_DIR)

configure: $(BUILD_DIR)/CMakeCache.txt

$(BUILD_DIR)/CMakeCache.txt: CMakeLists.txt src/ui/CMakeLists.txt src/qtplugin/CMakeLists.txt \
		src/shell/CMakeLists.txt src/android/CMakeLists.txt apps/CMakeLists.txt tools/CMakeLists.txt
	cmake -S . -B $(BUILD_DIR) -G Ninja -DCMAKE_INSTALL_PREFIX=$(PREFIX) $(CMAKE_FLAGS)

ui: configure
	cmake --build $(BUILD_DIR) --target finnui

qtplugin: configure
	cmake --build $(BUILD_DIR) --target finnplatformtheme finnstyle

compositor:
	$(MAKE) -C src/comp

session:
	$(MAKE) -C src/session

darwin:
	$(MAKE) -C src/darwin

ohos:
	$(MAKE) -C src/ohos

android:
	$(MAKE) -C src/android

shell: configure
	cmake --build $(BUILD_DIR) --target finn-shell

apps: configure
	cmake --build $(BUILD_DIR) --target \
		finn-wallpaper finn-files finn-settings finn-terminal \
		finn-notes finn-store finn-viewer finn-monitor

tools: configure
	cmake --build $(BUILD_DIR) --target finn-open finn-cursors

# The host suite: the registry, the tokens and the base image wiring. It needs
# no display and no device.
test:
	$(MAKE) -C tests

test-unit:
	$(MAKE) -C tests/unit run

check: test test-unit

lint:
	@scripts/lint.sh
	@./tools/check-branding.sh

install: all
	$(MAKE) -C src/comp install PREFIX=$(PREFIX) DESTDIR=$(DESTDIR)
	$(MAKE) -C src/session install PREFIX=$(PREFIX) DESTDIR=$(DESTDIR)
	DESTDIR=$(DESTDIR) cmake --install $(BUILD_DIR) --prefix $(PREFIX)
	install -d $(DESTDIR)/usr/share/finn
	install -m644 assets/apps.registry $(DESTDIR)/usr/share/finn/apps.registry
	install -m644 VERSION $(DESTDIR)/usr/share/finn/version
	install -d $(DESTDIR)/usr/share/finn/sdk/template
	cp -r sdk/template/. $(DESTDIR)/usr/share/finn/sdk/template/
	install -d $(DESTDIR)/usr/share/finn/icons
	install -m644 assets/icons/shell/*.svg assets/icons/apps/*.svg \
		$(DESTDIR)/usr/share/finn/icons/
	install -d $(DESTDIR)/usr/share/finn/models
	install -m644 assets/models/*.glb $(DESTDIR)/usr/share/finn/models/
	install -d $(DESTDIR)/usr/share/finn/pointers
	install -m644 assets/pointers/*.svg $(DESTDIR)/usr/share/finn/pointers/
	install -d $(DESTDIR)/usr/share/icons
	QT_QPA_PLATFORM=offscreen $(BUILD_DIR)/tools/finn-cursors assets/pointers $(DESTDIR)/usr/share/icons
	install -d $(DESTDIR)/usr/share/finn/wallpaper
	install -m644 assets/wallpaper/default.webp assets/wallpaper/setup.webp \
		$(DESTDIR)/usr/share/finn/wallpaper/
	install -d $(DESTDIR)/usr/share/fonts/OTF
	install -m644 assets/fonts/FixelDisplay/*.otf $(DESTDIR)/usr/share/fonts/OTF/
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m755 tools/finn-display $(DESTDIR)$(PREFIX)/bin/finn-display
	install -m755 tools/finn-sdk $(DESTDIR)$(PREFIX)/bin/finn-sdk
	install -m755 tools/finn-win $(DESTDIR)$(PREFIX)/bin/finn-win
	install -d $(DESTDIR)$(PREFIX)/share/applications
	install -m644 assets/finn-win.desktop $(DESTDIR)$(PREFIX)/share/applications/finn-win.desktop
	install -m644 assets/finn-flatpak.desktop $(DESTDIR)$(PREFIX)/share/applications/finn-flatpak.desktop
	install -m644 assets/finn-darwin.desktop $(DESTDIR)$(PREFIX)/share/applications/finn-darwin.desktop
	install -m644 assets/finn-ohos.desktop $(DESTDIR)$(PREFIX)/share/applications/finn-ohos.desktop
	install -m644 assets/finn-android.desktop $(DESTDIR)$(PREFIX)/share/applications/finn-android.desktop
	install -m755 src/ota/finn-update $(DESTDIR)$(PREFIX)/bin/finn-update
	install -d $(DESTDIR)$(PREFIX)/bin/finn_ota
	install -m644 src/ota/finn_ota/*.py $(DESTDIR)$(PREFIX)/bin/finn_ota/
	install -d $(DESTDIR)/usr/share/finn/ota
	install -m644 ota/grub-ab.cfg $(DESTDIR)/usr/share/finn/ota/grub-ab.cfg
	install -d $(DESTDIR)/usr/lib/systemd/system
	install -m644 base/finn-update-check.service \
		$(DESTDIR)/usr/lib/systemd/system/finn-update-check.service
	install -m644 base/finn-update-check.timer \
		$(DESTDIR)/usr/lib/systemd/system/finn-update-check.timer
	install -m644 base/finn-boot-health.service \
		$(DESTDIR)/usr/lib/systemd/system/finn-boot-health.service
	install -d $(DESTDIR)/usr/bin
	ln -sf $(PREFIX)/bin/finn-display $(DESTDIR)/usr/bin/finn-display
	ln -sf $(PREFIX)/bin/finn-sdk $(DESTDIR)/usr/bin/finn-sdk
	ln -sf $(PREFIX)/bin/finn-open $(DESTDIR)/usr/bin/finn-open
	ln -sf $(PREFIX)/bin/finn-cursors $(DESTDIR)/usr/bin/finn-cursors
	ln -sf $(PREFIX)/bin/finn-win $(DESTDIR)/usr/bin/finn-win
	ln -sf $(PREFIX)/bin/finn-darwin $(DESTDIR)/usr/bin/finn-darwin
	ln -sf $(PREFIX)/bin/finn-ohos $(DESTDIR)/usr/bin/finn-ohos
	ln -sf $(PREFIX)/bin/finn-android $(DESTDIR)/usr/bin/finn-android
	@if [ -z "$(DESTDIR)" ] && [ -w /etc/ld.so.conf.d ]; then \
		printf '%s\n' "$(PREFIX)/lib" > /etc/ld.so.conf.d/finn.conf; \
		ldconfig 2>/dev/null || true; \
	fi

deploy:
	FINN_HOST=$(VM) vm/deploy.sh

clean:
	rm -rf $(BUILD_DIR)
	$(MAKE) -C src/comp clean
	$(MAKE) -C src/session clean
	$(MAKE) -C src/darwin clean
	$(MAKE) -C src/ohos clean
	$(MAKE) -C src/android clean
	$(MAKE) -C tests/unit clean
	rm -rf tests/__pycache__ tools/__pycache__

help:
	@echo "targets: all ui qtplugin compositor session darwin ohos android shell apps tools test test-unit lint install deploy clean"

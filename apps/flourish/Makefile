BUILD_DIR ?= build

.PHONY: all run register clean

all:
	cmake -B $(BUILD_DIR) -G Ninja
	cmake --build $(BUILD_DIR)

run: all
	WAYLAND_DISPLAY=$${WAYLAND_DISPLAY:-/run/finn/wayland-0} \
	QT_QPA_PLATFORMTHEME=$${QT_QPA_PLATFORMTHEME:-finn} \
	QT_STYLE_OVERRIDE=$${QT_STYLE_OVERRIDE:-finn} \
	./$(BUILD_DIR)/finn-flourish

register: all
	finn-sdk register flourish "Flourish" "$(CURDIR)/$(BUILD_DIR)/finn-flourish" browser "Chromium browser for FinnOS"

clean:
	rm -rf $(BUILD_DIR)

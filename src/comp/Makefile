# Thin wrapper around the unified CMake build for finn-comp so legacy
# `make -C src/comp` invocations continue working seamlessly.

BUILD_DIR ?= ../../build
PREFIX    ?= /usr/local
DESTDIR   ?=

.PHONY: all install clean

all:
	@cmake -S ../.. -B $(BUILD_DIR) -G Ninja -DCMAKE_INSTALL_PREFIX=$(PREFIX) -DCMAKE_BUILD_TYPE=Release
	@cmake --build $(BUILD_DIR) --target finn-comp
	@cp -f $(BUILD_DIR)/src/comp/finn-comp ./finn-comp

install: all
	install -Dm755 finn-comp $(DESTDIR)$(PREFIX)/bin/finn-comp

clean:
	rm -f finn-comp

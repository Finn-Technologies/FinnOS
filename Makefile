# FinnOS — the top-level build.
#
#   make            build everything
#   make test       run the test suite
#   make install    install onto this machine (needs root)
#   make deploy     build, install and restart on the dev VM
#   make clean      remove every build artefact
#
# The tree is split the way the system is: a shared core, a compositor, a
# control plane, and the tools that only ever run on the host.

SUBDIRS   = src/core src/compositor src/control src/tools

PREFIX  ?= /usr/local
DESTDIR ?=
VM      ?= finn@10.211.55.6

.PHONY: all core compositor control tools test test-c test-rewrite check lint install clean deploy \
        docs help

all: core compositor control tools

core:
	$(MAKE) -C src/core

compositor: core
	$(MAKE) -C src/compositor

control: core
	$(MAKE) -C src/control

tools:
	$(MAKE) -C src/tools

# The tests do not need the C toolchain: they cover the parts that can be
# tested without a display, and they run anywhere Python does.
test:
	$(MAKE) -C tests

# The C tests need the compiler but no device: framing and the settings
# store are exercised against a socket pair and a temporary directory.
test-c:
	$(MAKE) -C tests/c run

# The rewrite's own tests: the modules that are already C++ and need no
# display. They build with the host compiler, so they run before the VM.
test-rewrite:
	$(MAKE) -C tests/rewrite run

check: test test-c test-rewrite

lint:
	@scripts/lint.sh

install: all
	$(MAKE) -C src/compositor install PREFIX=$(PREFIX) DESTDIR=$(DESTDIR)
	$(MAKE) -C src/control install PREFIX=$(PREFIX) DESTDIR=$(DESTDIR)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m755 src/pkg/finnpkg $(DESTDIR)$(PREFIX)/bin/finnpkg
	install -d $(DESTDIR)$(PREFIX)/lib/finn
	for helper in src/pkg/finn-winget-install scripts/finn-winget-install; do \
		if [ -f "$$helper" ]; then \
			install -m755 "$$helper" "$(DESTDIR)$(PREFIX)/lib/finn/$${helper##*/}"; \
		fi; \
	done
	install -d $(DESTDIR)/usr/share/finn
	install -m644 VERSION $(DESTDIR)/usr/share/finn/version
	install -m644 assets/apps.registry $(DESTDIR)/usr/share/finn/apps.registry
	install -d $(DESTDIR)/usr/share/finn/icons
	install -m644 assets/icons/shell/*.svg assets/icons/apps/*.svg $(DESTDIR)/usr/share/finn/icons/
	install -d $(DESTDIR)/usr/share/finn/wallpaper
	install -m644 assets/wallpaper/default.webp assets/wallpaper/setup.webp $(DESTDIR)/usr/share/finn/wallpaper/
	install -d $(DESTDIR)/usr/share/fonts/OTF
	install -m644 assets/fonts/FixelDisplay/*.otf $(DESTDIR)/usr/share/fonts/OTF/

# Build on the dev VM, install there and restart the compositor. The VM is
# the only machine with the DRM headers and a real display, so it is where
# the shell is actually run.
deploy:
	FINN_HOST=$(VM) vm/deploy.sh

clean:
	@for d in $(SUBDIRS); do $(MAKE) -C $$d clean; done
	@$(MAKE) -C tests/c clean
	@rm -rf tests/__pycache__ sdk/finnsdk/__pycache__ sdk/finnsdk/fuic/__pycache__

help:
	@echo "targets: all test test-c lint install deploy clean"
	@echo "         core compositor control tools"

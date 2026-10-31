SUBDIRS = src/ui src/comp src/session src/shell apps/files apps/settings apps/wallpaper

PREFIX  ?= /usr/local
DESTDIR ?=
VM      ?= finn@10.211.55.6

.PHONY: all ui compositor session shell apps test test-unit check lint \
        install clean deploy help

all: ui compositor session shell apps

ui:
	$(MAKE) -C src/ui

compositor:
	$(MAKE) -C src/comp

session:
	$(MAKE) -C src/session

shell: ui
	$(MAKE) -C src/shell

apps: ui
	$(MAKE) -C apps/files
	$(MAKE) -C apps/settings
	$(MAKE) -C apps/wallpaper

# The host suite: the registry, the tokens and the base image wiring. It needs
# no display and no device.
test:
	$(MAKE) -C tests

test-unit:
	$(MAKE) -C tests/unit run

check: test test-unit

lint:
	@scripts/lint.sh

install: all
	$(MAKE) -C src/ui install PREFIX=$(PREFIX) DESTDIR=$(DESTDIR)
	$(MAKE) -C src/comp install PREFIX=$(PREFIX) DESTDIR=$(DESTDIR)
	$(MAKE) -C src/session install PREFIX=$(PREFIX) DESTDIR=$(DESTDIR)
	$(MAKE) -C src/shell install PREFIX=$(PREFIX) DESTDIR=$(DESTDIR)
	$(MAKE) -C apps/files install PREFIX=$(PREFIX) DESTDIR=$(DESTDIR)
	$(MAKE) -C apps/settings install PREFIX=$(PREFIX) DESTDIR=$(DESTDIR)
	$(MAKE) -C apps/wallpaper install PREFIX=$(PREFIX) DESTDIR=$(DESTDIR)
	install -d $(DESTDIR)/usr/share/finn
	install -m644 assets/apps.registry $(DESTDIR)/usr/share/finn/apps.registry
	install -m644 VERSION $(DESTDIR)/usr/share/finn/version
	install -d $(DESTDIR)/usr/share/finn/icons
	install -m644 assets/icons/shell/*.svg assets/icons/apps/*.svg \
		$(DESTDIR)/usr/share/finn/icons/
	install -d $(DESTDIR)/usr/share/finn/wallpaper
	install -m644 assets/wallpaper/default.webp assets/wallpaper/setup.webp \
		$(DESTDIR)/usr/share/finn/wallpaper/
	install -d $(DESTDIR)/usr/share/fonts/OTF
	install -m644 assets/fonts/FixelDisplay/*.otf $(DESTDIR)/usr/share/fonts/OTF/

deploy:
	FINN_HOST=$(VM) vm/deploy.sh

clean:
	@for d in $(SUBDIRS); do $(MAKE) -C $$d clean; done
	@$(MAKE) -C tests/unit clean
	@rm -rf tests/__pycache__ tools/__pycache__

help:
	@echo "targets: all test test-unit lint install deploy clean"

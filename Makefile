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

.PHONY: all core compositor control tools test check install clean deploy \
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

test: all
	$(MAKE) -C tests

check: test

install: all
	$(MAKE) -C src/compositor install PREFIX=$(PREFIX) DESTDIR=$(DESTDIR)
	$(MAKE) -C src/control install PREFIX=$(PREFIX) DESTDIR=$(DESTDIR)

# Build on the dev VM, install there and restart the compositor. The VM is
# the only machine with the DRM headers and a real display, so it is where
# the shell is actually run.
deploy:
	FINN_HOST=$(VM) vm/deploy.sh

clean:
	@for d in $(SUBDIRS); do $(MAKE) -C $$d clean; done
	@rm -rf tests/__pycache__ sdk/python/**/__pycache__

help:
	@echo "targets: all test install deploy clean"
	@echo "         core compositor control tools"

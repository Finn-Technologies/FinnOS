.PHONY: help doctor build check test format format-check lint

help:
	./tools/finn help

doctor:
	./tools/finn doctor

build:
	./tools/finn build

check:
	./tools/finn check

test:
	./tools/finn test

format:
	./tools/finn format

format-check:
	./tools/finn format-check

lint:
	./tools/finn lint

boot:
	./tools/finn build-boot

image:
	./tools/finn image

run:
	./tools/finn run

run-headless:
	./tools/finn run-headless

test-boot:
	./tools/finn test-boot

test-exceptions:
	./tools/finn test-exceptions

test-memory-map:
	./tools/finn test-memory-map

test-page-allocator:
	./tools/finn test-page-allocator

check-all:
	./tools/finn check-all

clean:
	./tools/finn clean

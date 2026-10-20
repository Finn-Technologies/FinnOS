# The test suite.
#
# Plain python3 and the standard library: a suite that needs a wheel is
# a suite nobody runs. The tests read the tree and talk to a fake
# control plane, so they need no device and no VM.
#
#   make            run the suite
#   make test       the same thing
#   make clean      throw the byte-code away

PYTHON ?= python3

.PHONY: all test clean

all: test

test:
	$(PYTHON) -m unittest discover -v -s . -t .

clean:
	rm -rf __pycache__ *.pyc

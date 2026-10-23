# The C tests: the parts of the system that can be exercised without a
# display, a device or root.
#
# The sanitizers are part of the target rather than an afterthought: the
# framing and the settings store are small, and a bounds bug in either is
# exactly the kind a normal run does not show. A compiler without them can
# still run the same tests with `make SAN=`.

CC     ?= cc
CFLAGS ?= -O1 -g -Wall -Wextra -std=gnu11
SAN    ?= -fsanitize=address,undefined -fno-omit-frame-pointer

CORE   = ../../src/core
BINS   = test_finnconn test_finnsettings

all: $(BINS)

run: $(BINS)
	./test_finnconn
	./test_finnsettings

test_finnconn: test_finnconn.c $(CORE)/finnconn.c $(CORE)/finnconn.h
	$(CC) $(CFLAGS) $(SAN) -I$(CORE) -o $@ test_finnconn.c $(CORE)/finnconn.c

test_finnsettings: test_finnsettings.c $(CORE)/finnsettings.c $(CORE)/finnsettings.h
	$(CC) $(CFLAGS) $(SAN) -I$(CORE) -o $@ test_finnsettings.c $(CORE)/finnsettings.c

clean:
	rm -f $(BINS)
	rm -rf *.dSYM

.PHONY: all run clean

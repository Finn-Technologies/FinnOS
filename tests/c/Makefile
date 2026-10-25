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
COMP   = ../../src/compositor
CTRL   = ../../src/control
BINS   = test_finnconn test_finnsettings test_finnvg test_finnblur test_finntaskbar test_finnfbin test_finncompat test_finnsettingsapp_pages test_finndraw

# The Settings page test links only navigation metadata; macOS uses compile-only
# shims for Linux headers and its native dead-section linker flag.
TEST_PLATFORM_INCLUDES =
TEST_GC_SECTIONS = -Wl,--gc-sections
ifeq ($(shell uname -s),Darwin)
TEST_PLATFORM_INCLUDES += -Icompat/darwin
TEST_GC_SECTIONS = -Wl,-dead_strip
endif

all: $(BINS)

run: $(BINS)
	./test_finnconn
	./test_finnsettings
	./test_finnvg
	./test_finnblur
	./test_finntaskbar
	./test_finnfbin
	./test_finncompat
	./test_finnsettingsapp_pages
	./test_finndraw

test_finnconn: test_finnconn.c $(CORE)/finnconn.c $(CORE)/finnconn.h
	$(CC) $(CFLAGS) $(SAN) -I$(CORE) -o $@ test_finnconn.c $(CORE)/finnconn.c

test_finnsettings: test_finnsettings.c $(CORE)/finnsettings.c $(CORE)/finnsettings.h
	$(CC) $(CFLAGS) $(SAN) -I$(CORE) -o $@ test_finnsettings.c $(CORE)/finnsettings.c

test_finnvg: test_finnvg.c $(COMP)/finnvg.c $(COMP)/finnvg.h
	$(CC) $(CFLAGS) $(SAN) -I$(COMP) -o $@ test_finnvg.c $(COMP)/finnvg.c -lm

test_finnblur: test_finnblur.c $(COMP)/finnblur.c $(COMP)/finnblur.h
	$(CC) $(CFLAGS) $(SAN) -I$(COMP) -o $@ test_finnblur.c $(COMP)/finnblur.c

test_finntaskbar: test_finntaskbar.c $(CORE)/finntaskbar.c $(CORE)/finntaskbar.h \
                  $(CORE)/finnsettings.c $(CORE)/finnsettings.h
	$(CC) $(CFLAGS) $(SAN) -I$(CORE) -o $@ test_finntaskbar.c \
	      $(CORE)/finntaskbar.c $(CORE)/finnsettings.c

test_finnfbin: test_finnfbin.c $(CORE)/finnfbin.c $(CORE)/finnfbin.h
	$(CC) $(CFLAGS) $(SAN) -I$(CORE) -o $@ test_finnfbin.c $(CORE)/finnfbin.c -lm

test_finncompat: test_finncompat.c $(CTRL)/finncompat.c $(CTRL)/finncompat.h $(CORE)/finnproto.h
	$(CC) $(CFLAGS) $(SAN) -I$(CTRL) -I$(CORE) -o $@ test_finncompat.c $(CTRL)/finncompat.c

test_finnsettingsapp_pages: test_finnsettingsapp_pages.c $(COMP)/finnsettingsapp.c $(COMP)/finnsettingsapp.h
	$(CC) $(CFLAGS) $(SAN) -ffunction-sections -fdata-sections $(TEST_PLATFORM_INCLUDES) -I$(COMP) -I$(CORE) $(TEST_GC_SECTIONS) -o $@ test_finnsettingsapp_pages.c $(COMP)/finnsettingsapp.c -lm

test_finndraw: test_finndraw.c $(COMP)/finndraw.c $(COMP)/finndraw.h
	$(CC) $(CFLAGS) $(SAN) -ffunction-sections -fdata-sections -I$(COMP) $(TEST_GC_SECTIONS) -o $@ test_finndraw.c $(COMP)/finndraw.c -lm

clean:
	rm -f $(BINS)
	rm -rf *.dSYM

.PHONY: all run clean

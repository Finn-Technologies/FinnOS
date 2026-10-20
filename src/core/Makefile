# finncore — the shared core of FinnOS.
#
# A static library, not a directory of loose files: these are the parts the
# compositor and the control plane both need, and there is exactly one copy
# of the protocol, the socket helpers, the state reader and the app registry
# in the tree.
#
# The rule the split follows: anything in here is *data and plumbing*. The
# compositor and the control plane may both use it, but it may not know
# anything about either of them.

CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -std=gnu11
PKGS     = alsa
CFLAGS  += $(shell pkg-config --cflags $(PKGS))
LDLIBS  += $(shell pkg-config --libs $(PKGS)) -lm

LIB      = libfinncore.a

SRCS     = finnapps.c finnaudio.c finnstate.c finnsock.c
OBJS     = $(SRCS:.c=.o)

HDRS     = finnapps.h finnaudio.h finnproto.h finnstate.h finnsock.h

all: $(LIB)

$(LIB): $(OBJS)
	$(AR) rcs $@ $(OBJS)

%.o: %.c $(HDRS)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(LIB)

.PHONY: all clean

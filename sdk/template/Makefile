CXX      ?= c++
CXXFLAGS ?= -std=c++20 -O2 -g -Wall -Wextra -Wpedantic
FINNUI_CFLAGS ?= $(shell pkg-config --cflags finnui 2>/dev/null || echo -I/usr/local/include)
FINNUI_LIBS   ?= $(shell pkg-config --libs --static finnui 2>/dev/null || \
                   echo /usr/local/lib/libfinnui.a -lwayland-client -lxkbcommon -lfreetype -lwebp -lm)

BIN = finn-counter

$(BIN): main.cpp
	$(CXX) $(CXXFLAGS) $(FINNUI_CFLAGS) -o $@ main.cpp $(FINNUI_LIBS)

install: $(BIN)
	install -Dm755 $(BIN) $(DESTDIR)$(PREFIX)/bin/$(BIN)

clean:
	rm -f $(BIN)

.PHONY: install clean

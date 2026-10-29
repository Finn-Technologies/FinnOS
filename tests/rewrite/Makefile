CXX      ?= c++
CXXFLAGS ?= -std=c++20 -O2 -g -Wall -Wextra -Wpedantic -Werror
SAN      ?= -fsanitize=address,undefined -fno-omit-frame-pointer

REGISTRY_SRC = ../../src/session/app_registry.cpp
REGISTRY_HDR = ../../src/session/app_registry.hpp
BINS         = test_app_registry

all: $(BINS)

test_app_registry: test_app_registry.cpp $(REGISTRY_SRC) $(REGISTRY_HDR)
	$(CXX) $(CXXFLAGS) $(SAN) -I../../src -o $@ test_app_registry.cpp $(REGISTRY_SRC)

run: $(BINS)
	./test_app_registry

clean:
	rm -f $(BINS)

.PHONY: all run clean

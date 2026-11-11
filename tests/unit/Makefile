CC       ?= cc
CXX      ?= c++
CXXFLAGS ?= -std=c++20 -O2 -g -Wall -Wextra -Wpedantic -Werror
SAN      ?= -fsanitize=address,undefined -fno-omit-frame-pointer

REGISTRY_SRC = ../../src/session/app_registry.cpp
REGISTRY_HDR = ../../src/session/app_registry.hpp
BINS         = test_app_registry test_animation test_session_client test_search

all: $(BINS)

test_app_registry: test_app_registry.cpp $(REGISTRY_SRC) $(REGISTRY_HDR)
	$(CXX) $(CXXFLAGS) $(SAN) -I../../src -o $@ test_app_registry.cpp $(REGISTRY_SRC)

test_animation: test_animation.cpp ../../src/ui/finn/animation.hpp
	$(CXX) $(CXXFLAGS) $(SAN) -I../../src/ui -o $@ test_animation.cpp

test_session_client: test_session_client.cpp ../../src/session/client.cpp \
		../../src/session/client.hpp ../../src/common/paths.hpp
	$(CXX) $(CXXFLAGS) $(SAN) -I../../src -o $@ test_session_client.cpp \
		../../src/session/client.cpp

test_search: test_search.cpp ../../src/shell/search.cpp \
		../../src/shell/search.hpp ../../src/session/client.hpp
	$(CXX) $(CXXFLAGS) $(SAN) -I../../src -o $@ test_search.cpp \
		../../src/shell/search.cpp

run: $(BINS)
	./test_app_registry
	./test_animation
	./test_session_client
	./test_search
	python3 test_tokens.py

clean:
	rm -f $(BINS)

.PHONY: all run clean

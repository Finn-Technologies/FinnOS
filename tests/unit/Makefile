CC       ?= cc
CXX      ?= c++
CXXFLAGS ?= -std=c++20 -O2 -g -Wall -Wextra -Wpedantic -Werror
SAN      ?= -fsanitize=address,undefined -fno-omit-frame-pointer

REGISTRY_SRCS = ../../src/session/app_registry.cpp \
                ../../src/compat/desktop_entry.cpp \
                ../../src/compat/wine_runner.cpp \
                ../../src/system/sys_files.cpp
BINS          = test_app_registry test_animation test_spring test_session_client \
                test_search test_compat test_sys_files

all: $(BINS)

test_app_registry: test_app_registry.cpp $(REGISTRY_SRCS) \
		../../src/session/app_registry.hpp
	$(CXX) $(CXXFLAGS) $(SAN) -I../../src -o $@ test_app_registry.cpp $(REGISTRY_SRCS)

test_animation: test_animation.cpp ../../src/ui/finn/animation.hpp \
		../../src/common/paths.hpp
	$(CXX) $(CXXFLAGS) $(SAN) -I../../src/ui -o $@ test_animation.cpp

test_spring: test_spring.cpp ../../src/ui/finn/spring.hpp \
		../../src/ui/finn/animation.hpp ../../src/common/paths.hpp
	$(CXX) $(CXXFLAGS) $(SAN) -I../../src/ui -o $@ test_spring.cpp

test_session_client: test_session_client.cpp ../../src/session/client.cpp \
		../../src/session/client.hpp ../../src/common/paths.hpp
	$(CXX) $(CXXFLAGS) $(SAN) -I../../src -o $@ test_session_client.cpp \
		../../src/session/client.cpp

test_search: test_search.cpp ../../src/shell/search.cpp \
		../../src/shell/search.hpp ../../src/session/client.hpp
	$(CXX) $(CXXFLAGS) $(SAN) -I../../src -o $@ test_search.cpp \
		../../src/shell/search.cpp

test_compat: test_compat.cpp ../../src/compat/desktop_entry.cpp \
		../../src/compat/wine_runner.cpp ../../src/compat/desktop_entry.hpp \
		../../src/compat/wine_runner.hpp
	$(CXX) $(CXXFLAGS) $(SAN) -I../../src -o $@ test_compat.cpp \
		../../src/compat/desktop_entry.cpp ../../src/compat/wine_runner.cpp

test_sys_files: test_sys_files.cpp ../../src/system/sys_files.cpp \
		../../src/system/sys_files.hpp ../../src/common/paths.hpp
	$(CXX) $(CXXFLAGS) $(SAN) -I../../src -o $@ test_sys_files.cpp \
		../../src/system/sys_files.cpp

run: $(BINS)
	./test_app_registry
	./test_animation
	./test_spring
	./test_session_client
	./test_search
	./test_compat
	./test_sys_files
	python3 test_tokens.py

clean:
	rm -f $(BINS)

.PHONY: all run clean

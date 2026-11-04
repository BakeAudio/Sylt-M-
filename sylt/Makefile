CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Werror

all: syltc

syltc: src/*.cpp src/*.hpp sylt_prelude.hpp
	$(CXX) $(CXXFLAGS) -o syltc src/*.cpp

test: syltc
	./tests/run_tests.sh

install: syltc
	mkdir -p ~/.local/share/sylt
	cp -r syltc sylt_prelude.hpp lib ~/.local/share/sylt/

clean:
	rm -f syltc

.PHONY: all test install clean

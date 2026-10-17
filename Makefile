# Makefile for vibe-apps: builds against the vibe engine checked out next to this repo (../vibe)
PARALLEL ?= --parallel 4
VIBE_ROOT ?= ../vibe

all: main

build/build.ninja:
	@test -d $(VIBE_ROOT)/.deps/usr || { echo "no $(VIBE_ROOT)/.deps/usr: clone vibe next to this repo and run 'make deps' there"; exit 1; }
	cmake --preset linux -DVIBE_ROOT=$(abspath $(VIBE_ROOT))

main: build/build.ninja
	cmake --build build $(PARALLEL)

test: main
	ctest --test-dir build --output-on-failure

clean:
	rm -rf build

.PHONY: all main test clean

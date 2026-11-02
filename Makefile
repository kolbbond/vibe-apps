# Makefile for vibe-apps: builds against the vibe engine checked out next to this repo (../vibe)
PARALLEL ?= --parallel 4
VIBE_ROOT ?= ../vibe
# user-writable by default, so plasma "Save preset" works in the installed copy
PREFIX ?= $(HOME)/.local

all: main

build/build.ninja:
	@test -d $(VIBE_ROOT)/.deps/usr || { echo "no $(VIBE_ROOT)/.deps/usr: clone vibe next to this repo and run 'make deps' there"; exit 1; }
	cmake --preset linux -DVIBE_ROOT=$(abspath $(VIBE_ROOT))

main: build/build.ninja
	cmake --build build $(PARALLEL)

test: main
	ctest --test-dir build --output-on-failure

# vvavviz -> $(PREFIX)/bin, presets -> $(PREFIX)/share/vvavviz, engine + deps libs -> $(PREFIX)/lib/vibe
install: main
	cmake --install build --prefix $(PREFIX)

# removes exactly what the last install wrote (build/install_manifest.txt)
uninstall:
	@test -f build/install_manifest.txt || { echo "nothing to uninstall: no build/install_manifest.txt"; exit 1; }
	xargs -d '\n' rm -f < build/install_manifest.txt
	rm -f build/install_manifest.txt

clean:
	rm -rf build

.PHONY: all main test install uninstall clean

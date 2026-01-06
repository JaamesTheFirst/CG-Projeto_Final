.PHONY: all configure build run clean distclean help

# Simple wrapper Makefile (required for course submission).
# Uses CMake underneath, but provides the expected:
#   - compile/link: `make` / `make build`
#   - run:          `make run` (optionally: `make run ARGS="lvl2"`)
#
# Notes:
# - Build outputs go to ./build
# - The executable is `super_mario_proto` (see CMakeLists.txt)

BUILD_DIR ?= build
TARGET ?= super_mario_proto

all: build

help:
	@echo "Targets:"
	@echo "  make | make build        - configure + build (CMake)"
	@echo "  make run ARGS=\"lvl2\"     - run the game (optional args)"
	@echo "  make clean               - clean build artifacts (keeps ./build)"
	@echo "  make distclean           - remove ./build entirely"

configure:
	@cmake -S . -B "$(BUILD_DIR)"

build: configure
	@cmake --build "$(BUILD_DIR)" -j

run: build
	@"./$(BUILD_DIR)/$(TARGET)" $(ARGS)

clean:
	@cmake --build "$(BUILD_DIR)" --target clean

distclean:
	@rm -rf "$(BUILD_DIR)"


# Makefile for iapRemote

# Default target: Build using CMake in a 'build' directory
all:
	@mkdir -p build && cmake -B build -S . $(EXTRA_CMAKE_FLAGS) && cmake --build build -j$$(nproc)

# Static target: Build a static binary using CMake with STATIC_BUILD=ON
static:
	@mkdir -p build_static && cmake -B build_static -S . -DSTATIC_BUILD=ON && cmake --build build_static -j$$(nproc)

# Clean target: Remove build directories and root CMake artifacts
clean:
	@rm -rf build build_static
	@rm -f CMakeCache.txt cmake_install.cmake Makefile.cmake
	@rm -rf CMakeFiles/

.PHONY: all static clean

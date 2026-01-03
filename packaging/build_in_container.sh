#!/bin/bash
# iapRemote Containerized Build Script
# This script builds the application and creates .deb packages using a clean container.

set -e

# Configuration
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VCPKG_HOST_DIR="/opt/vcpkg"
TARGET_OS=${1:-ubuntu} # Default to ubuntu, can also be "debian"

if [ "$TARGET_OS" == "debian" ]; then
    IMAGE="debian:12"
    PKG_SCRIPT="packaging/debian_dist/create_deb.sh"
else
    IMAGE="ubuntu:22.04"
    PKG_SCRIPT="packaging/ubuntu_dist/create_deb.sh"
fi

echo "Starting container build for $TARGET_OS using $IMAGE..."

# Ensure we have the container tool
CONTAINER_TOOL=$(command -v docker || command -v podman)
if [ -z "$CONTAINER_TOOL" ]; then
    echo "Error: Neither docker nor podman found."
    exit 1
fi

# Run the container
# We mount:
# 1. The project root to /src
# 2. The host vcpkg directory to /opt/vcpkg
# 3. We run as root inside the container to install build essentials
$CONTAINER_TOOL run --rm \
    -v "$PROJECT_ROOT:/src" \
    -v "$VCPKG_HOST_DIR:/opt/vcpkg" \
    -w /src \
    "$IMAGE" /bin/bash -c "
        apt-get update && \
        apt-get install -y cmake g++ pkg-config libgtkmm-3.0-dev libvte-2.91-dev libssl-dev dpkg-dev make curl zip unzip tar git nlohmann-json3-dev && \
        echo 'Build dependencies installed. Starting packaging script...' && \
        mkdir -p /src/.vcpkg_cache && \
        export VCPKG_BINARY_SOURCES='clear;files,/src/.vcpkg_cache,readwrite' && \
        export VCPKG_INSTALL_OPTIONS='--clean-after-build' && \
        export BUILD_DIR=build_container && \
        export EXTRA_CMAKE_FLAGS='-DCMAKE_TOOLCHAIN_FILE=/opt/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_INSTALLED_DIR=/src/vcpkg_installed_container' && \
        bash $PKG_SCRIPT
    "

echo "Container build finished successfully."

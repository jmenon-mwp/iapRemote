#!/bin/bash
# iapRemote Containerized Build Orchestrator
# This script runs the robust packaging scripts inside a clean container.

set -e

# Configuration
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VCPKG_HOST_DIR="$PROJECT_ROOT/build_vcpkg"
TARGET_OS=${1:-debian} # debian, ubuntu, or rocky
TARGET_VER=$2          # e.g., 11 or 12 for debian, 20.04 or 22.04 for ubuntu, 8 or 9 for rpm
PKG_VERSION=$3         # e.g., 1.0.1 (optional)

if [ "$TARGET_OS" == "debian" ]; then
    IMAGE_TAG=${TARGET_VER:-12}
    IMAGE="debian:$IMAGE_TAG"
    DISTRO_ID="debian$IMAGE_TAG"
    PKG_SCRIPT="packaging/debian_dist/create_deb.sh"
elif [ "$TARGET_OS" == "ubuntu" ]; then
    IMAGE_TAG=${TARGET_VER:-22.04}
    IMAGE="ubuntu:$IMAGE_TAG"
    DISTRO_ID="ubuntu$IMAGE_TAG"
    PKG_SCRIPT="packaging/ubuntu_dist/create_deb.sh"
elif [ "$TARGET_OS" == "rocky" ]; then
    IMAGE_TAG=${TARGET_VER:-9}
    IMAGE="rockylinux:$IMAGE_TAG"
    DISTRO_ID="rhel$IMAGE_TAG"
    PKG_SCRIPT="packaging/rpm_dist/create_rpm.sh"
else
    echo "Error: Unsupported TARGET_OS '$TARGET_OS'. Use debian, ubuntu, or rocky."
    exit 1
fi

echo "Orchestrating container build for $TARGET_OS ($IMAGE_TAG) using $IMAGE..."

# Ensure we have the container tool
CONTAINER_TOOL=$(command -v docker || command -v podman)
if [ -z "$CONTAINER_TOOL" ]; then
    echo "Error: Neither docker nor podman found."
    exit 1
fi

# Run the container
# We mount:
# 1. The project root to /src (where we work)
# 2. The host vcpkg directory to /opt/vcpkg (for persistence/speed)
$CONTAINER_TOOL run --rm \
    -v "$PROJECT_ROOT:/src" \
    -v "$VCPKG_HOST_DIR:/opt/vcpkg" \
    -w /src \
    "$IMAGE" /bin/bash -c "
        export VCPKG_ROOT=/opt/vcpkg && \
        export DISTRO_ID=$DISTRO_ID && \
        ${PKG_VERSION:+export PKG_VERSION=$PKG_VERSION && } \
        bash $PKG_SCRIPT
    "

echo "Container build process finished. Check the 'output' directory for your .deb file."

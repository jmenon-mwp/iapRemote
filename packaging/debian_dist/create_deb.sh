#!/bin/bash
# iapRemote - Comprehensive Build and Debian Packaging Script
# This script handles dependency installation, compilation with vcpkg, and .deb creation.

set -e

# Configuration
PKG_NAME="iapremote"
VERSION="1.0.0"
ARCH="amd64"
DISTRO_ID=${DISTRO_ID:-debian}
DEB_FILE="${PKG_NAME}_${VERSION}_${DISTRO_ID}_${ARCH}.deb"
STAGE_DIR="deb_stage"
VCPKG_ROOT=${VCPKG_ROOT:-/opt/vcpkg}
GITHUB_URL="https://github.com/jmenon-mwp/iapRemote"

echo "=== iapRemote Build and Packaging Starting ==="
echo "Target: Debian/Ubuntu Package"

# 1. System Dependencies
if [ "$BUILD_SKIP_DEPS" != "1" ]; then
    echo "[1/5] Installing system dependencies..."
    export DEBIAN_FRONTEND=noninteractive
    apt-get update && apt-get install -y \
        build-essential \
        cmake \
        git \
        pkg-config \
        libgtkmm-3.0-dev \
        libvte-2.91-dev \
        libssl-dev \
        nlohmann-json3-dev \
        curl \
        zip \
        unzip \
        tar \
        dpkg-dev
fi

# 2. vcpkg Setup
echo "[2/5] Setting up vcpkg..."
# If VCPKG_ROOT doesn't have a vcpkg binary, we need to setup
if [ ! -f "$VCPKG_ROOT/vcpkg" ]; then
    if [ ! -d "$VCPKG_ROOT/.git" ]; then
        echo "vcpkg not found at $VCPKG_ROOT. Cloning..."
        # If it's a mounted empty dir, git clone might complain if we don't handle it
        # But usually git clone into an empty dir is fine.
        git clone https://github.com/microsoft/vcpkg.git "$VCPKG_ROOT"
    fi
    echo "Bootstrapping vcpkg..."
    "$VCPKG_ROOT/bootstrap-vcpkg.sh"
fi

echo "Ensuring google-cloud-cpp[core,iap] is installed in vcpkg (Classic Mode)..."
# We ensure classic mode by passing --classic to avoid any manifest detection
"$VCPKG_ROOT/vcpkg" install "google-cloud-cpp[core,iap]" --classic

# 3. Source Code Setup
echo "[3/5] Preparing source code..."
# Determine if we are already in the iapRemote source tree
if [ -f "CMakeLists.txt" ] && grep -q "project(iapRemote)" CMakeLists.txt; then
    echo "Already in source tree."
    PROJECT_ROOT=$(pwd)
else
    echo "Cloning source from $GITHUB_URL..."
    if [ -d "iapRemote" ]; then rm -rf iapRemote; fi
    git clone "$GITHUB_URL"
    cd iapRemote
    PROJECT_ROOT=$(pwd)
fi

# 4. Compilation
echo "[4/5] Compiling iapRemote..."
BUILD_DIR="build_deb_dist"
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

# Note: We use the vcpkg toolchain to find our static libraries
cmake -B "$BUILD_DIR" -S . \
    -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
    -DSTATIC_BUILD=ON \
    -DCMAKE_BUILD_TYPE=Release

cmake --build "$BUILD_DIR" -j$(nproc)

# 5. Packaging
echo "[5/5] Creating Debian package..."
cd "$PROJECT_ROOT"
rm -rf "$STAGE_DIR"
mkdir -p "$STAGE_DIR/usr/bin"
mkdir -p "$STAGE_DIR/usr/share/applications"
mkdir -p "$STAGE_DIR/usr/share/icons/hicolor/scalable/apps"
mkdir -p "$STAGE_DIR/usr/share/iapRemote"
mkdir -p "$STAGE_DIR/DEBIAN"

# Copy artifacts
cp "$BUILD_DIR/iapRemote" "$STAGE_DIR/usr/bin/"
cp styles.css "$STAGE_DIR/usr/share/iapRemote/"
cp packaging/debian_dist/iapRemote.desktop "$STAGE_DIR/usr/share/applications/"
cp icon.svg "$STAGE_DIR/usr/share/icons/hicolor/scalable/apps/iapRemote.svg"
cp packaging/debian_dist/DEBIAN/control "$STAGE_DIR/DEBIAN/"
cp packaging/debian_dist/DEBIAN/postinst "$STAGE_DIR/DEBIAN/"
cp packaging/debian_dist/DEBIAN/postrm "$STAGE_DIR/DEBIAN/"

# Permissions
chmod 755 "$STAGE_DIR/DEBIAN/postinst"
chmod 755 "$STAGE_DIR/DEBIAN/postrm"
chmod 755 "$STAGE_DIR/usr/bin/iapRemote"
chmod 644 "$STAGE_DIR/usr/share/applications/iapRemote.desktop"
chmod 644 "$STAGE_DIR/usr/share/icons/hicolor/scalable/apps/iapRemote.svg"
chmod 644 "$STAGE_DIR/usr/share/iapRemote/styles.css"

# Build final deb
dpkg-deb --build "$STAGE_DIR" "$DEB_FILE"

# Final placement
# If we cloned into a subfolder, we might want to move it to the parent
mkdir -p "$PROJECT_ROOT/output"
mv "$DEB_FILE" "$PROJECT_ROOT/output/"

echo "Success! Package created in: $PROJECT_ROOT/output/$DEB_FILE"
echo "=== iapRemote Build and Packaging Finished ==="

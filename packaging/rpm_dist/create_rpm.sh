#!/bin/bash
# iapRemote - Comprehensive Build and RPM Packaging Script
# This script handles dependency installation (dnf), compilation with vcpkg, and .rpm creation.

set -e

# Configuration
PKG_NAME="iapRemote"
VERSION="${PKG_VERSION:-1.0.0}"
ARCH="x86_64"
DISTRO_ID=${DISTRO_ID:-rhel9}
RPMBUILD_ROOT="$(pwd)/rpmbuild"
VCPKG_ROOT=${VCPKG_ROOT:-/opt/vcpkg}
GITHUB_URL="https://github.com/jmenon-mwp/iapRemote"

echo "=== iapRemote Build and RPM Packaging Starting ==="
echo "Target: RPM Package ($DISTRO_ID)"

# 1. System Dependencies (dnf)
if [ "$BUILD_SKIP_DEPS" != "1" ]; then
    echo "[1/5] Installing system dependencies..."
    if command -v dnf >/dev/null 2>&1; then
        # Install EPEL and CRB (Code Ready Builder)
        dnf install -y --allowerasing epel-release
        dnf install -y --allowerasing 'dnf-command(config-manager)'
        dnf config-manager --set-enabled crb

        # Install Essential Build Tools first
        dnf install -y --allowerasing \
            gcc-c++ \
            libstdc++-static \
            cmake \
            make \
            git \
            pkgconfig \
            curl \
            zip \
            unzip \
            tar \
            rpm-build \
            perl-generators \
            kernel-headers \
            perl-IPC-Cmd \
            perl-FindBin \
            perl-File-Compare \
            perl-File-Copy \
            perl-Text-Template

        # Install GUI and Dev libraries (some might be in EPEL/CRB)
        dnf install -y --allowerasing \
            gtkmm30-devel \
            vte291-devel \
            openssl-devel \
            nlohmann-json-devel
    fi
fi

# 2. vcpkg Setup
echo "[2/5] Setting up vcpkg..."
if [ ! -f "$VCPKG_ROOT/vcpkg" ]; then
    if [ ! -d "$VCPKG_ROOT/.git" ]; then
        echo "vcpkg not found at $VCPKG_ROOT. Cloning..."
        git clone https://github.com/microsoft/vcpkg.git "$VCPKG_ROOT"
    fi
    echo "Bootstrapping vcpkg..."
    "$VCPKG_ROOT/bootstrap-vcpkg.sh"
fi

# Define custom triplet for release optimization
mkdir -p "$VCPKG_ROOT/triplets/community"
cat > "$VCPKG_ROOT/triplets/community/x64-linux-release.cmake" <<EOF
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_BUILD_TYPE release)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)
EOF

echo "Ensuring google-cloud-cpp[core,iap] is installed (Release Only)..."
"$VCPKG_ROOT/vcpkg" install "google-cloud-cpp[core,iap]" \
    --triplet=x64-linux-release \
    --overlay-triplets="$VCPKG_ROOT/triplets/community" \
    --clean-after-build \
    --classic

# 3. Source Code Setup
echo "[3/5] Preparing source code..."
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
BUILD_DIR="build_rpm_dist"
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

cmake -B "$BUILD_DIR" -S . \
    -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
    -DVCPKG_TARGET_TRIPLET=x64-linux-release \
    -DVCPKG_OVERLAY_TRIPLETS="$VCPKG_ROOT/triplets/community" \
    -DSTATIC_BUILD=ON \
    -DCMAKE_BUILD_TYPE=Release

cmake --build "$BUILD_DIR" -j$(nproc)

# 5. Packaging (RPM)
echo "[5/5] Creating RPM package..."
rm -rf "$RPMBUILD_ROOT"
mkdir -p "$RPMBUILD_ROOT"/{BUILD,RPMS,SOURCES,SPECS,SRPMS}

cp packaging/rpm_dist/SPECS/iapRemote.spec "$RPMBUILD_ROOT/SPECS/"

# Run rpmbuild in binary-only mode (-bb)
# We pass the absolute path to our project tree so the spec can find our files
rpmbuild -bb \
    --define "_topdir $RPMBUILD_ROOT" \
    --define "project_root $PROJECT_ROOT" \
    --define "PKG_VERSION $VERSION" \
    "$RPMBUILD_ROOT/SPECS/iapRemote.spec"

# Final placement
mkdir -p "$PROJECT_ROOT/output"
find "$RPMBUILD_ROOT/RPMS" -name "*.rpm" -exec mv {} "$PROJECT_ROOT/output/" \;

echo "Success! RPM Package(s) created in: $PROJECT_ROOT/output/"
echo "=== iapRemote Build and Packaging Finished ==="

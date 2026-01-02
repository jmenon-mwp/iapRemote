#!/bin/bash
# iapRemote RPM Package Creation Script (RHEL, CentOS, Rocky Linux)

set -e

# Configuration
PKG_NAME="iapRemote"
VERSION="1.0.0"
RPMBUILD_ROOT="$(pwd)/rpmbuild"

# Move to the project root directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../../" && pwd)"
cd "$PROJECT_ROOT"

echo "Cleaning up previous build artifacts..."
rm -rf "$RPMBUILD_ROOT"
mkdir -p "$RPMBUILD_ROOT"/{BUILD,RPMS,SOURCES,SPECS,SRPMS}

# Create source tarball
echo "Creating source tarball..."
# We create a temporary directory to structure the tarball correctly
TEMP_TAR_DIR="tmp_rpm_tar"
rm -rf "$TEMP_TAR_DIR"
mkdir -p "$TEMP_TAR_DIR/$PKG_NAME-$VERSION"
cp -r * "$TEMP_TAR_DIR/$PKG_NAME-$VERSION/"
# Remove the temp build artifacts from the tarball
rm -rf "$TEMP_TAR_DIR/$PKG_NAME-$VERSION/build"
rm -rf "$TEMP_TAR_DIR/$PKG_NAME-$VERSION/deb_stage"
rm -rf "$TEMP_TAR_DIR/$PKG_NAME-$VERSION/rpmbuild"

tar -czf "$RPMBUILD_ROOT/SOURCES/$PKG_NAME-$VERSION.tar.gz" -C "$TEMP_TAR_DIR" "$PKG_NAME-$VERSION"
rm -rf "$TEMP_TAR_DIR"

# Copy the spec file
cp packaging/rpm_dist/SPECS/iapRemote.spec "$RPMBUILD_ROOT/SPECS/"

echo "Starting rpmbuild..."
if command -v rpmbuild >/dev/null 2>&1; then
    rpmbuild -ba --define "_topdir $RPMBUILD_ROOT" --define "extra_cmake_flags $EXTRA_CMAKE_FLAGS" "$RPMBUILD_ROOT/SPECS/iapRemote.spec"
    
    echo "Success! RPM packages created in $RPMBUILD_ROOT/RPMS/"
    ls -R "$RPMBUILD_ROOT/RPMS/"
else
    echo "Error: rpmbuild command not found."
    echo "To build this on an RPM-based system, you typically need to install 'rpm-build' and 'rpmdevtools'."
    echo "The build environment is prepared in $RPMBUILD_ROOT"
    exit 1
fi

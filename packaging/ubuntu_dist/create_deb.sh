#!/bin/bash
# iapRemote Ubuntu Package Creation Script (also works for Linux Mint)

set -e

# Configuration
PKG_NAME="iapremote"
VERSION="1.0.0"
ARCH="amd64"
DEB_FILE="${PKG_NAME}_${VERSION}_${ARCH}.deb"
STAGE_DIR="deb_stage"

# Move to the project root directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../../" && pwd)"
cd "$PROJECT_ROOT"

echo "Building project from $PROJECT_ROOT..."
make all

# Setup staging directory
echo "Setting up staging directory..."
rm -rf "$STAGE_DIR"
mkdir -p "$STAGE_DIR/usr/bin"
mkdir -p "$STAGE_DIR/usr/share/applications"
mkdir -p "$STAGE_DIR/usr/share/icons/hicolor/scalable/apps"
mkdir -p "$STAGE_DIR/usr/share/iapRemote"
mkdir -p "$STAGE_DIR/DEBIAN"

# Copy files to staging
echo "Copying files..."
cp build/iapRemote "$STAGE_DIR/usr/bin/"
cp styles.css "$STAGE_DIR/usr/share/iapRemote/"
cp packaging/ubuntu_dist/iapRemote.desktop "$STAGE_DIR/usr/share/applications/"
cp icon.svg "$STAGE_DIR/usr/share/icons/hicolor/scalable/apps/iapRemote.svg"
cp packaging/ubuntu_dist/DEBIAN/control "$STAGE_DIR/DEBIAN/"
cp packaging/ubuntu_dist/DEBIAN/postinst "$STAGE_DIR/DEBIAN/"
cp packaging/ubuntu_dist/DEBIAN/postrm "$STAGE_DIR/DEBIAN/"

# Set permissions
chmod 755 "$STAGE_DIR/DEBIAN/postinst"
chmod 755 "$STAGE_DIR/DEBIAN/postrm"
chmod 755 "$STAGE_DIR/usr/bin/iapRemote"
chmod 644 "$STAGE_DIR/usr/share/applications/iapRemote.desktop"
chmod 644 "$STAGE_DIR/usr/share/icons/hicolor/scalable/apps/iapRemote.svg"
chmod 644 "$STAGE_DIR/usr/share/iapRemote/styles.css"

# Create .deb package
echo "Creating .deb package..."
dpkg-deb --build "$STAGE_DIR" "$DEB_FILE"

# Move the deb file to the packaging directory
mv "$DEB_FILE" "packaging/ubuntu_dist/"

# Cleanup
echo "Cleaning up..."
rm -rf "$STAGE_DIR"

echo "Success! Package created: packaging/ubuntu_dist/$DEB_FILE"

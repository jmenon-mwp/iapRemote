#!/bin/bash
# iapRemote - macOS Build and DMG Packaging Script
set -e

# Configuration
PKG_NAME="iapRemote"
VERSION="${PKG_VERSION:-1.0.0}"
# 1. Architecture & Dependency Setup
UNIVERSAL_BUILD="${UNIVERSAL_BUILD:-true}" # Default to universal for release
ARCH=$(uname -m)
OUTPUT_DMG="${PKG_NAME}_${VERSION}_macos_${ARCH}.dmg" # Keep this for now, might need adjustment for universal
BUILD_DIR="build_macos"
STAGE_DIR="macos_stage"
APP_BUNDLE="${STAGE_DIR}/${PKG_NAME}.app"

echo "=== iapRemote macOS Build ==="

if [ "$UNIVERSAL_BUILD" == "true" ]; then
    echo "[1/4] Configuring for Universal Binary (arm64 + x86_64)..."
    VCPKG_TRIPLET_ARCHS="arm64;x86_64"
    TRIPLET="universal-osx-release"
    CMAKE_ARCH_FLAGS="-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64"
    OUTPUT_DMG="${PKG_NAME}_${VERSION}_macos_universal.dmg" # Update DMG name for universal
else
    echo "[1/4] Configuring for native Binary ($ARCH)..."
    if [ "$ARCH" == "arm64" ]; then
        VCPKG_TRIPLET_ARCHS="arm64"
    else
        VCPKG_TRIPLET_ARCHS="x64"
    fi
    TRIPLET="${VCPKG_TRIPLET_ARCHS}-osx-release"
    CMAKE_ARCH_FLAGS=""
    OUTPUT_DMG="${PKG_NAME}_${VERSION}_macos_${ARCH}.dmg" # Ensure DMG name is correct for native
fi

if ! command -v brew &> /dev/null; then
    echo "Homebrew not found. Installing..."
    /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

    # Add brew to PATH
    if [ -d "/opt/homebrew/bin" ]; then
        eval "$(/opt/homebrew/bin/brew shellenv)"
    elif [ -d "/usr/local/bin" ]; then
        eval "$(/usr/local/bin/brew shellenv)"
    fi
fi

# We use Homebrew for high-level UI libs (gtkmm, vte)
# NOTE: Homebrew usually installs architecture-specific binaries.
# For true universal builds, these dependencies may need to be built from source or via vcpkg.
echo "Updating Homebrew and installing dependencies..."
brew update
brew install cmake pkg-config gtkmm3 vte3 nlohmann-json openssl@3 dylibbundler create-dmg

# Install vcpkg and google-cloud-cpp
VCPKG_ROOT="$HOME/vcpkg"
if [ ! -f "$VCPKG_ROOT/vcpkg" ]; then
  git clone https://github.com/microsoft/vcpkg.git "$VCPKG_ROOT"
  "$VCPKG_ROOT/bootstrap-vcpkg.sh"
fi

echo "Creating custom triplet: $TRIPLET"
mkdir -p "$VCPKG_ROOT/triplets/community"
cat > "$VCPKG_ROOT/triplets/community/${TRIPLET}.cmake" <<EOF
set(VCPKG_TARGET_ARCHITECTURE x64) # Base architecture
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_BUILD_TYPE release)
set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_ARCHITECTURES "${VCPKG_TRIPLET_ARCHS}")
EOF

"$VCPKG_ROOT/vcpkg" install "google-cloud-cpp[core,iap]" \
    --triplet="$TRIPLET" \
    --overlay-triplets="$VCPKG_ROOT/triplets/community" \
    --clean-after-build \
    --classic

# 2. Compilation
echo "[2/4] Compiling..."
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

# Point CMake to Homebrew OpenSSL and Google Cloud OCP
OPENSSL_ROOT_DIR=$(brew --prefix openssl@3)

cmake -B "$BUILD_DIR" -S . \
    -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
    -DVCPKG_TARGET_TRIPLET="$TRIPLET" \
    -DOPENSSL_ROOT_DIR="$OPENSSL_ROOT_DIR" \
    -DAPP_VERSION="${VERSION}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$(brew --prefix)" \
    ${CMAKE_ARCH_FLAGS}

cmake --build "$BUILD_DIR" -j$(sysctl -n hw.ncpu)

# 3. App Bundle Creation
echo "[3/4] Creating App Bundle..."
rm -rf "$STAGE_DIR"
mkdir -p "$APP_BUNDLE/Contents/MacOS"
mkdir -p "$APP_BUNDLE/Contents/Resources"

# Copy Binary
cp "$BUILD_DIR/iapRemote" "$APP_BUNDLE/Contents/MacOS/"

# Copy Resources
cp styles.css "$APP_BUNDLE/Contents/Resources/"
# Convert SVG icon to ICNS if possible, or just copy for now
# (Proper ICNS creation requires 'iconutil' and an .iconset directory)
cp icon.svg "$APP_BUNDLE/Contents/Resources/icon.svg"

# Create Info.plist
cat > "$APP_BUNDLE/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key>
    <string>iapRemote</string>
    <key>CFBundleIdentifier</key>
    <string>com.jmenon.iapRemote</string>
    <key>CFBundleName</key>
    <string>iapRemote</string>
    <key>CFBundleIconFile</key>
    <string>icon.svg</string>
    <key>CFBundleShortVersionString</key>
    <string>${VERSION}</string>
    <key>CFBundleVersion</key>
    <string>${VERSION}</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>LSMinimumSystemVersion</key>
    <string>10.15</string>
    <key>NSHighResolutionCapable</key>
    <true/>
</dict>
</plist>
EOF

# Bundle Libraries (dylibbundler)
# This walks the binary and copies referenced dylibs into the bundle
# Note: This might require sudo permissions in some setups, but usually fine in CI
echo "Bundling dynamic libraries..."
dylibbundler -of -b -x "$APP_BUNDLE/Contents/MacOS/iapRemote" \
    -d "$APP_BUNDLE/Contents/Libs" \
    -p "@executable_path/../Libs/"

# 4. Create DMG
echo "[4/4] Creating DMG..."
mkdir -p output
rm -f "output/$OUTPUT_DMG"

# Using create-dmg utility (brew install create-dmg)
create-dmg \
  --volname "iapRemote Installer" \
  --volicon "icon.svg" \
  --window-pos 200 120 \
  --window-size 800 400 \
  --icon-size 100 \
  --icon "iapRemote.app" 200 190 \
  --hide-extension "iapRemote.app" \
  --app-drop-link 600 185 \
  "output/$OUTPUT_DMG" \
  "$APP_BUNDLE"

echo "Success! Created output/$OUTPUT_DMG"

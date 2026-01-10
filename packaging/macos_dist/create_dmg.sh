#!/bin/bash
# iapRemote - macOS Build and DMG Packaging Script
set -e

# Configuration
PKG_NAME="iapRemote"
VERSION="${PKG_VERSION:-1.0.0}"
ARCH=$(uname -m)
OUTPUT_DMG="${PKG_NAME}_${VERSION}_macos_${ARCH}.dmg"
BUILD_DIR="build_macos"
STAGE_DIR="macos_stage"
APP_BUNDLE="${STAGE_DIR}/${PKG_NAME}.app"

echo "=== iapRemote macOS Build ==="
echo "Target: macOS ${ARCH}"

# 1. Install Dependencies
echo "[1/4] Installing dependencies..."
if ! command -v brew &> /dev/null; then
    echo "Homebrew not found. Installing..."
    /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
    
    # Add brew to PATH for the current session (standard locations for Intel and ARM)
    if [ -d "/opt/homebrew/bin" ]; then
        eval "$(/opt/homebrew/bin/brew shellenv)"
    elif [ -d "/usr/local/bin" ]; then
        eval "$(/usr/local/bin/brew shellenv)"
    fi
fi

# We use Homebrew for speed on macOS CI, instead of compiling boost/grpc/etc via vcpkg
brew update
brew install cmake pkg-config gtkmm3 vte3 nlohmann-json openssl@3 google-cloud-cpp dylibbundler create-dmg

# 2. Compilation
echo "[2/4] Compiling..."
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

# Point CMake to Homebrew OpenSSL and Google Cloud OCP
OPENSSL_ROOT_DIR=$(brew --prefix openssl@3)

cmake -B "$BUILD_DIR" -S . \
    -DOPENSSL_ROOT_DIR="$OPENSSL_ROOT_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$(brew --prefix)"

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

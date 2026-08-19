#!/usr/bin/env bash
# Build OpenEkklesia as a macOS .app bundle.
#
# NOTE: This MUST run on a Mac (Intel or Apple Silicon) — it cannot be
# cross-compiled from Linux. For an Intel Mac build, run on the Intel Mac with
# an Intel Qt 6 install (e.g. Homebrew under /usr/local).
#
# Requires: Xcode command-line tools, CMake, Qt 6 (Core Widgets Sql Charts
# PrintSupport), and macdeployqt (shipped with Qt).
set -euo pipefail
cd "$(dirname "$0")"

# Locate macdeployqt (Homebrew Intel vs Apple Silicon paths).
MACDEPLOYQT="$(command -v macdeployqt || true)"
[ -z "$MACDEPLOYQT" ] && [ -x /usr/local/opt/qt/bin/macdeployqt ] && MACDEPLOYQT=/usr/local/opt/qt/bin/macdeployqt
[ -z "$MACDEPLOYQT" ] && [ -x /opt/homebrew/opt/qt/bin/macdeployqt ] && MACDEPLOYQT=/opt/homebrew/opt/qt/bin/macdeployqt
if [ -z "$MACDEPLOYQT" ]; then
  echo "ERROR: macdeployqt not found. Install Qt 6 (brew install qt) and re-run."
  exit 1
fi

# 1. Build the application
cmake -S . -B build-macos -DCMAKE_BUILD_TYPE=Release
cmake --build build-macos -j"$(sysctl -n hw.ncpu)"

# 2. Assemble the .app bundle structure
APP=OpenEkklesia.app
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp build-macos/OpenEkklesia "$APP/Contents/MacOS/"

# Optional icon (convert a 1024x1024 PNG with `sips`/`iconutil` first if you
# have one; otherwise the app uses the default icon).
if [ -f dist/OpenEkklesia.icns ]; then
  cp dist/OpenEkklesia.icns "$APP/Contents/Resources/OpenEkklesia.icns"
fi

cat > "$APP/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>OpenEkklesia</string>
  <key>CFBundleDisplayName</key><string>OpenEkklesia</string>
  <key>CFBundleIdentifier</key><string>org.openekklesia.app</string>
  <key>CFBundleVersion</key><string>1.5.0</string>
  <key>CFBundleShortVersionString</key><string>1.5.0</string>
  <key>CFBundleExecutable</key><string>OpenEkklesia</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>LSMinimumSystemVersion</key><string>11.0</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>CFBundleIconFile</key><string>OpenEkklesia</string>
</dict>
</plist>
PLIST

# 3. Bundle the Qt frameworks, plugins, and translations into the app.
"$MACDEPLOYQT" "$APP" -always-overwrite

echo "✅ Built: $APP  (drag it into /Applications)"
echo "   Intel Mac:    run on an Intel Mac to get an x86_64 binary."
echo "   Apple Silicon: run on an M-series Mac to get an arm64 binary."
echo "   (To verify architecture:  file $APP/Contents/MacOS/OpenEkklesia)"

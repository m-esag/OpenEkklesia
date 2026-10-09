#!/usr/bin/env bash
# Build OpenEkklesia as a self-contained Linux AppImage.
# Produces: dist/OpenEkklesia-<arch>.AppImage
set -euo pipefail
cd "$(dirname "$0")"

ARCH="$(uname -m)"

# 1. Build the application
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"

# 2. Tooling (downloaded once into tools/, gitignored)
mkdir -p tools
if [ ! -x tools/linuxdeploy.AppImage ]; then
  echo "Downloading AppImage tooling..."
  curl -sL -o tools/linuxdeploy.AppImage \
    https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${ARCH}.AppImage
  curl -sL -o tools/linuxdeploy-plugin-qt.AppImage \
    https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-${ARCH}.AppImage
  curl -sL -o tools/appimagetool.AppImage \
    https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-${ARCH}.AppImage
  chmod +x tools/*.AppImage
fi

export APPIMAGE_EXTRACT_AND_RUN=1
export PATH="$PWD/tools:$PATH"
# qmake is often not on PATH (e.g. Fedora installs it under /usr/lib64/qt6/bin
# with only a qmake-qt6 symlink in /usr/bin); probe the common locations.
export QMAKE="$(command -v qmake-qt6 || command -v qmake6 || command -v qmake || true)"
if [ -z "$QMAKE" ]; then
  for cand in /usr/lib64/qt6/bin/qmake /usr/lib/qt6/bin/qmake /usr/lib/qt/bin/qmake; do
    [ -x "$cand" ] && QMAKE="$cand" && break
  done
fi
[ -n "$QMAKE" ] || { echo "ERROR: could not locate qmake for linuxdeploy-plugin-qt" >&2; exit 1; }

# 3. Assemble the AppDir with the Qt plugin (bundles libs + plugins)
rm -rf dist/OpenEkklesia.AppDir
linuxdeploy.AppImage --appdir dist/OpenEkklesia.AppDir \
  --executable build/OpenEkklesia --plugin qt

# Desktop entry + icon (required by AppImage packaging)
cp assets/openekklesia.png dist/OpenEkklesia.AppDir/OpenEkklesia.png
cat > dist/OpenEkklesia.AppDir/OpenEkklesia.desktop <<'DESKTOP'
[Desktop Entry]
Type=Application
Name=OpenEkklesia
Comment=Church Management System (ChMS)
Exec=OpenEkklesia
Icon=OpenEkklesia
Categories=Office;Finance;Database;
Terminal=false
DESKTOP

# Qt 6 linuxdeploy skips AppRun generation; write the standard launcher.
cat > dist/OpenEkklesia.AppDir/AppRun <<'RUN'
#!/bin/sh
SELF=$(readlink -f "$0")
HERE=${SELF%/*}
export PATH="${HERE}/usr/bin/:${HERE}/usr/sbin/:${HERE}/bin/:${HERE}/sbin/:${PATH}"
export LD_LIBRARY_PATH="${HERE}/usr/lib/:${HERE}/usr/lib/x86_64-linux-gnu/:${HERE}/usr/lib64/:${LD_LIBRARY_PATH}"
export QT_QPA_PLATFORM_PLUGIN_PATH="${HERE}/usr/plugins/platforms"
# Fedora/GNOME default to Wayland; the bundle ships only the xcb plugin,
# so pick it explicitly (via XWayland) unless the user overrides. Prevents
# the "could not locate wayland platform plugin" warning on Wayland sessions.
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-xcb}"
export QT_PLUGIN_PATH="${HERE}/usr/plugins:${QT_PLUGIN_PATH}"
exec "${HERE}/usr/bin/OpenEkklesia" "$@"
RUN
chmod +x dist/OpenEkklesia.AppDir/AppRun

# Bundle libstdc++/libgcc: they CAN ride along safely (unlike glibc), and
# doing so removes the host toolchain's GLIBCXX baseline as a runtime
# constraint on older targets. AppRun's LD_LIBRARY_PATH prefers them.
for _lib in libstdc++.so.6 libgcc_s.so.1; do
  _src=""
  for _cand in "/usr/lib64/$_lib" "/usr/lib/x86_64-linux-gnu/$_lib" "/usr/lib/$_lib"; do
    if [ -e "$_cand" ]; then _src="$_cand"; break; fi
  done
  if [ -n "$_src" ]; then
    cp -L "$_src" dist/OpenEkklesia.AppDir/usr/lib/
  else
    echo "WARNING: could not locate $_lib for bundling" >&2
  fi
done

# Include the offscreen platform plugin so the app also runs headless
# (real desktops use the bundled xcb plugin). -print -quit avoids the
# `find | head` SIGPIPE race that trips set -euo pipefail.
if [ ! -f dist/OpenEkklesia.AppDir/usr/plugins/platforms/libqoffscreen.so ]; then
  OFFSCREEN="$(find /usr -name libqoffscreen.so -print -quit 2>/dev/null || true)"
  [ -n "$OFFSCREEN" ] && cp "$OFFSCREEN" dist/OpenEkklesia.AppDir/usr/plugins/platforms/ || true
fi

# 4. Package into a single-file AppImage
APPDIR_ABS="$PWD/dist/OpenEkklesia.AppDir"
OUT_ABS="$PWD/dist/OpenEkklesia-${ARCH}.AppImage"
appimagetool.AppImage "$APPDIR_ABS" "$OUT_ABS"
echo "✅ Built: dist/OpenEkklesia-${ARCH}.AppImage"

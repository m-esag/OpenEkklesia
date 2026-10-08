#!/usr/bin/env bash
# ==============================================================================
# OpenEkklesia — macOS DMG / Disk Image Builder (Apple Silicon & Intel)
# ==============================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

echo "==> 🏛️  OpenEkklesia macOS DMG Builder"
echo "==> Working Directory: ${ROOT_DIR}"

# 1. Detect Qt Installation
if command -v brew &>/dev/null; then
    QT_PREFIX="$(brew --prefix qt 2>/dev/null || brew --prefix qt@6 2>/dev/null || true)"
fi

if [ -z "${QT_PREFIX}" ] || [ ! -d "${QT_PREFIX}" ]; then
    if [ -d "/opt/homebrew/opt/qt" ]; then
        QT_PREFIX="/opt/homebrew/opt/qt"
    elif [ -d "/usr/local/opt/qt" ]; then
        QT_PREFIX="/usr/local/opt/qt"
    else
        echo "❌ Error: Qt 6 not found via Homebrew."
        echo "   Please install it by running: brew install qt cmake ninja"
        exit 1
    fi
fi

echo "==> Using Qt at: ${QT_PREFIX}"
MACDEPLOYQT="${QT_PREFIX}/bin/macdeployqt"

if [ ! -f "${MACDEPLOYQT}" ]; then
    echo "❌ Error: macdeployqt tool not found at ${MACDEPLOYQT}"
    exit 1
fi

BUILD_DIR="${ROOT_DIR}/build_mac"
rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"

# 2. Configure & Build Release .app
echo "==> Configuring CMake Release build..."
cmake -B "${BUILD_DIR}" -S "${ROOT_DIR}" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="${QT_PREFIX}"

echo "==> Building OpenEkklesia.app..."
cmake --build "${BUILD_DIR}" --config Release

APP_PATH="${BUILD_DIR}/OpenEkklesia.app"
if [ ! -d "${APP_PATH}" ]; then
    echo "❌ Error: App bundle not found at ${APP_PATH}"
    exit 1
fi

# 3. Bundle Qt Frameworks & Plugins with macdeployqt
echo "==> Packaging Qt frameworks and creating standalone .dmg disk image..."
"${MACDEPLOYQT}" "${APP_PATH}" -dmg -verbose=1

# 4. Move generated DMG to project root for easy sharing
GENERATED_DMG="$(find "${BUILD_DIR}" -maxdepth 2 -name "*.dmg" | head -n 1)"
DMG_NAME="OpenEkklesia-macOS-AppleSilicon.dmg"
DMG_PATH="${ROOT_DIR}/${DMG_NAME}"

if [ -n "${GENERATED_DMG}" ] && [ -f "${GENERATED_DMG}" ]; then
    mv "${GENERATED_DMG}" "${DMG_PATH}"
    echo ""
    echo "=============================================================================="
    echo "🎉 SUCCESS! Standalone macOS Disk Image created:"
    echo "📦 ${DMG_PATH} ($(du -h "${DMG_PATH}" | cut -f1))"
    echo "=============================================================================="
    echo "You can now share '${DMG_NAME}' directly with any Mac user!"
else
    echo "App bundle created at: ${APP_PATH}"
fi

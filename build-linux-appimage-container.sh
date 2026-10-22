#!/usr/bin/env bash
# Build the distributable AppImage inside an Ubuntu 22.04 container so the
# artifact's glibc floor is 2.35 (Linux Mint 21+, Ubuntu 22.04+, Debian 12+).
#
# Why: AppImages bundle their libraries, but glibc cannot be bundled. This
# host (OpenMandriva, glibc 2.44) produces artifacts that fail on stable
# distros with "version `GLIBC_2.4x' not found". Building in the container
# links everything against 2.35 instead.
#
# One-time setup:  sudo dnf install podman   (or docker)
# Build:           ./build-linux-appimage-container.sh
# Result:          dist/OpenEkklesia-x86_64.AppImage  (glibc 2.35 compatible)
set -euo pipefail
cd "$(dirname "$0")"

# Pick a container engine.
ENGINE=""
if command -v podman >/dev/null 2>&1; then ENGINE=podman;
elif command -v docker >/dev/null 2>&1; then ENGINE=docker;
else
  echo "ERROR: no container engine found." >&2
  echo "Install one, then re-run:  sudo dnf install podman" >&2
  exit 1
fi

echo "==> Building build image (Ubuntu 22.04 + Qt6)..."
$ENGINE build -f Dockerfile.build -t openekklesia-build .

echo "==> Building AppImage inside container (repo mounted read-write)..."
mkdir -p dist
$ENGINE run --rm \
    --user "$(id -u):$(id -g)" \
    -v "$PWD:/src" \
    -e HOME=/tmp \
    openekklesia-build ./build-linux-appimage.sh

echo ""
echo "==> Verify the artifact's glibc requirement (should print 2.35):"
objdump -T dist/OpenEkklesia.AppDir/usr/bin/OpenEkklesia 2>/dev/null \
  | grep -oE "GLIBC_[0-9.]+" | sort -Vu | tail -1
echo "✅ Done: dist/OpenEkklesia-x86_64.AppImage"

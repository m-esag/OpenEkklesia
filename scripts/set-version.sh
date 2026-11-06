#!/usr/bin/env bash
# Set the OpenEkklesia application version.
#
#   scripts/set-version.sh 0.8.6        # plain release
#   scripts/set-version.sh 0.8.6-rc1    # release candidate
#
# The version lives in exactly ONE place — project(OpenEkklesia VERSION ...)
# in CMakeLists.txt — and everything else derives from it:
#   - About dialog:      generated appversion.h (build-time)
#   - macOS bundle:      ${PROJECT_VERSION}
#   - Windows installer: build-windows-installer.ps1 parses CMakeLists.txt
#     and passes /DMyAppVersion to ISCC (this script also refreshes the
#     .iss fallback constant for direct-ISCC runs)
#   - build identity:    OE_BUILD_ID (git describe) stamps the exact source
#                        state into every binary automatically
#
# Version-policy note: WHEN to bump remains a human/release-coordinator
# decision; this script only removes the mechanical multi-site editing.
set -euo pipefail
cd "$(dirname "$0")/.."

if [ $# -ne 1 ]; then
    echo "Usage: scripts/set-version.sh <major.minor.patch[-suffix]>" >&2
    echo "Example: scripts/set-version.sh 0.8.6" >&2
    exit 1
fi

newVer="$1"
if ! printf '%s' "$newVer" | grep -qE '^[0-9]+\.[0-9]+\.[0-9]+(-[A-Za-z0-9.]+)?$'; then
    echo "ERROR: '$newVer' is not a valid X.Y.Z[-suffix] version." >&2
    exit 1
fi

# CMake project() VERSION accepts only numeric components: an -rc suffix is
# carried by the git TAG (and shown in About via git describe), not here.
numericVer="${newVer%%-*}"
suffix="${newVer#"$numericVer"}"   # "" or "-rc1"

oldVer="$(sed -n 's/^project(OpenEkklesia VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt | head -1)"
if [ -z "$oldVer" ]; then
    echo "ERROR: could not read the current version from CMakeLists.txt." >&2
    exit 1
fi

sed -i "s/^project(OpenEkklesia VERSION ${oldVer}/project(OpenEkklesia VERSION ${numericVer}/" CMakeLists.txt
sed -i "s/#define MyAppVersion \"${oldVer}\"/#define MyAppVersion \"${numericVer}\"/" installer/OpenEkklesia.iss

echo "Version: ${oldVer} -> ${numericVer}${suffix}"
echo ""
echo "Next steps (release coordinator):"
echo "  1. cmake --build build-windows --config Release && openekklesia_tests"
echo "  2. git commit -am \"v${newVer}\""
echo "  3. git tag -a v${newVer} -m \"OpenEkklesia ${newVer}\"   # About shows the tag via git describe"
echo "  4. .\\build-windows-installer.ps1   # setup exe name comes from CMakeLists"
if [ -n "$suffix" ]; then
    echo ""
    echo "NOTE: '$suffix' is recorded by the git tag, not by CMake (CMake VERSION"
    echo "accepts numeric components only). About will show it via git describe."
fi

#!/usr/bin/env bash
# Builds an AppImage for Absolute Simple Editor. Run from anywhere —
# paths below are resolved relative to this script, not the caller's
# working directory. See packaging/README.md for the overview of every
# package format in this directory.
#
# Requires: the same build dependencies as any other build of this
# project (CMake >= 3.20, a C11/C++20 toolchain, Qt6 Widgets), plus
# network access the *first* run only, to fetch linuxdeploy and its Qt
# plugin — cached under packaging/appimage/.tools/ afterward, never
# re-fetched once present.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="$REPO_ROOT/build-appimage"
APPDIR="$SCRIPT_DIR/AppDir"
TOOLS_DIR="$SCRIPT_DIR/.tools"

# Single source of truth: the same project(VERSION ...) CMakeLists.txt
# already carries (see docs/adr/0033) — not re-typed here.
PROJECT_VERSION="$(sed -n 's/^project(.*VERSION \([0-9.]*\).*/\1/p' "$REPO_ROOT/CMakeLists.txt")"
if [ -z "$PROJECT_VERSION" ]; then
    # project() spans multiple lines in this CMakeLists.txt — the
    # single-line sed above only catches a one-line form. Fall back to
    # scanning the next couple of lines for a bare "VERSION x.y.z".
    PROJECT_VERSION="$(grep -A2 '^project(' "$REPO_ROOT/CMakeLists.txt" | sed -n 's/^[[:space:]]*VERSION \([0-9.]*\).*/\1/p' | head -1)"
fi
VERSION_STAGE="$(sed -n 's/^set(ASE_VERSION_STAGE "\([a-z]*\)").*/\1/p' "$REPO_ROOT/CMakeLists.txt")"
: "${VERSION_STAGE:?ASE_VERSION_STAGE not found in CMakeLists.txt}"
export VERSION="${PROJECT_VERSION}-${VERSION_STAGE}"

mkdir -p "$TOOLS_DIR"

fetch_tool() {
    local name="$1" url="$2"
    local dest="$TOOLS_DIR/$name"
    if [ ! -x "$dest" ]; then
        echo "==> Fetching $name (first run only, cached under packaging/appimage/.tools/)"
        curl -L --fail -o "$dest" "$url"
        chmod +x "$dest"
    fi
}

# "continuous" is linuxdeploy's own rolling-release tag — there is no
# other stable channel for either tool as of this writing.
fetch_tool linuxdeploy "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
fetch_tool linuxdeploy-plugin-qt "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage"
export PATH="$TOOLS_DIR:$PATH"

echo "==> Configuring ($BUILD_DIR, Release)"
cmake -S "$REPO_ROOT" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DASE_BUILD_GUI=ON \
    -DASE_BUILD_TESTS=OFF

echo "==> Building"
cmake --build "$BUILD_DIR" -j"$(nproc)"

echo "==> Staging AppDir"
rm -rf "$APPDIR"
DESTDIR="$APPDIR" cmake --install "$BUILD_DIR" --prefix /usr

echo "==> Running linuxdeploy (bundles Qt6 + other shared libs)"
cd "$SCRIPT_DIR"
# linuxdeploy-plugin-qt finds *a* qmake on PATH to detect the Qt
# install to bundle — on a system with more than one Qt version
# installed (e.g. Arch, which ships qmake-qt5 as well), that can pick
# the wrong one and then fail with "Could not find Qt modules to
# deploy" because the detected major version doesn't match what
# ase_gui actually links. QMAKE pins it to the real Qt6 one explicitly.
if [ -z "${QMAKE:-}" ]; then
    for candidate in /usr/lib/qt6/bin/qmake6 /usr/bin/qmake6 /usr/lib/qt6/bin/qmake; do
        if [ -x "$candidate" ]; then
            export QMAKE="$candidate"
            break
        fi
    done
fi

# The linuxdeploy "continuous" build bundles its own `strip`, older
# than a rolling-release distro's own binutils — it doesn't understand
# newer ELF sections (seen here: `.relr.dyn`, RELR relative
# relocations) and aborts the whole run rather than just skipping that
# one file. NO_STRIP is linuxdeploy's own documented escape hatch;
# unstripped libraries just make the AppImage a little larger.
export NO_STRIP=1
# --appimage-extract-and-run: linuxdeploy and its plugin are themselves
# distributed as AppImages, which need FUSE to mount directly — this
# flag makes them extract-and-run instead, which works identically but
# doesn't depend on FUSE being available/permitted in the environment
# actually running this script (e.g. some CI containers).
#
# The flag alone isn't enough, though — a real bug found running this
# inside a FUSE-less container (see docs/adr/0045): linuxdeploy only
# applies --appimage-extract-and-run to *itself*; when it shells out to
# invoke linuxdeploy-plugin-qt (also an AppImage), it doesn't forward
# the flag, so the plugin still tries to FUSE-mount and dies with an
# opaque "exit code 127". APPIMAGE_EXTRACT_AND_RUN=1 is the same
# behavior as an env var instead of a CLI flag, so it reaches that
# child process too.
export APPIMAGE_EXTRACT_AND_RUN=1
"$TOOLS_DIR/linuxdeploy" --appimage-extract-and-run \
    --appdir "$APPDIR" \
    --executable "$APPDIR/usr/bin/ase_gui" \
    --desktop-file "$APPDIR/usr/share/applications/ase.desktop" \
    --icon-file "$APPDIR/usr/share/icons/hicolor/512x512/apps/ase.png" \
    --plugin qt \
    --output appimage

echo "==> Done — see packaging/appimage/*.AppImage"

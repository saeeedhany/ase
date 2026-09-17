#!/usr/bin/env bash
# Builds a .deb for Absolute Simple Editor directly with dpkg-deb — no
# debhelper/devscripts dependency, just cmake + dpkg-deb (both already
# needed/available anywhere Debian packaging matters). Run from
# anywhere; paths below are resolved relative to this script. See
# packaging/README.md for the overview of every package format here.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="$REPO_ROOT/build-deb"
PKGROOT="$SCRIPT_DIR/pkgroot"

PROJECT_VERSION="$(grep -A2 '^project(' "$REPO_ROOT/CMakeLists.txt" | sed -n 's/^[[:space:]]*VERSION \([0-9.]*\).*/\1/p' | head -1)"
VERSION_STAGE="$(sed -n 's/^set(ASE_VERSION_STAGE "\([a-z]*\)").*/\1/p' "$REPO_ROOT/CMakeLists.txt")"
: "${VERSION_STAGE:?ASE_VERSION_STAGE not found in CMakeLists.txt}"
DEB_VERSION="${PROJECT_VERSION}~${VERSION_STAGE}1"
ARCH="$(dpkg --print-architecture 2>/dev/null || echo amd64)"

echo "==> Configuring ($BUILD_DIR, Release)"
cmake -S "$REPO_ROOT" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DASE_BUILD_GUI=ON \
    -DASE_BUILD_TESTS=OFF

echo "==> Building"
cmake --build "$BUILD_DIR" -j"$(nproc)"

echo "==> Staging package root"
rm -rf "$PKGROOT"
DESTDIR="$PKGROOT" cmake --install "$BUILD_DIR" --prefix /usr
mkdir -p "$PKGROOT/DEBIAN"

# Real runtime deps, not a guess — ldd on the linked binary itself,
# mapped back to the packages that own each shared library. Qt6's own
# transitive deps (X11, ICU, ...) get pulled in for free. The `|| true`
# on the dpkg -S segment matters: on a non-Debian build host (this
# project is developed on Arch, whose dpkg has no package database at
# all) every lookup fails and dpkg -S exits non-zero — without it,
# `set -o pipefail` above would abort the whole script right here.
#
# readlink -f before dpkg -S matters too — a real bug found building
# this inside debian:bookworm: ldd reports paths through /lib, which on
# a usrmerge system (Debian/Ubuntu today) is a symlink to /usr/lib, but
# dpkg's own file database records the canonical /usr/lib path. dpkg -S
# does a literal path match, so every Qt6 library silently failed to
# resolve to its owning package and got dropped from Depends entirely
# — the .deb built "successfully" but declared no Qt6 dependency at
# all. Canonicalizing first fixes it regardless of which prefix ldd
# happens to report.
DEPENDS="$(
    { ldd "$PKGROOT/usr/bin/ase_gui" |
        awk '{print $3}' |
        grep '^/' |
        xargs -r -I{} readlink -f {} |
        sort -u |
        xargs -r dpkg -S 2>/dev/null || true; } |
        cut -d: -f1 |
        sort -u |
        paste -sd, -
)"
DEPENDS="${DEPENDS:-libqt6widgets6}" # fallback if dpkg -S can't resolve on a non-Debian build host

cat > "$PKGROOT/DEBIAN/control" <<EOF
Package: ase
Version: ${DEB_VERSION}
Section: editors
Priority: optional
Architecture: ${ARCH}
Depends: ${DEPENDS}
Maintainer: Saeed <alsaeedalbasi0ny@gmail.com>
Homepage: https://github.com/saeeedhany/ase
Description: A minimal, robust, blazingly fast, and aesthetically
 deliberate GUI text editor.
 Core/GUI are cleanly separated; the GUI is a Qt6 shell over a
 headless C engine. Licensed under the Apache License 2.0.
EOF

echo "==> Building .deb"
OUTPUT="$SCRIPT_DIR/ase_${DEB_VERSION}_${ARCH}.deb"
dpkg-deb --root-owner-group --build "$PKGROOT" "$OUTPUT"

echo "==> Done — $OUTPUT"

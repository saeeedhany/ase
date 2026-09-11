# Packaging

Three formats, all built on the same foundation: the `install()` rules
in `gui/CMakeLists.txt` (binary, `.desktop` file, icon — standard
freedesktop layout under `usr/`). Every script here just runs
`cmake --install` against that and wraps the result differently.

Shared assets: `linux/ase.desktop`, `linux/ase-icon-512.png` (the
app's `ase` wordmark logo, padded to a square transparent canvas —
`gui/resources/ase.png` is the un-padded original used inside the app
itself/the Qt resource bundle, not this file).

## AppImage

```sh
packaging/appimage/build-appimage.sh
```

Builds Release, stages an AppDir, runs `linuxdeploy` +
`linuxdeploy-plugin-qt` to bundle Qt6 and produce a self-contained
`.AppImage`. Needs network access the first run only, to fetch those
two tools (cached under `packaging/appimage/.tools/` after that).
Output lands in `packaging/appimage/`.

**Run this inside a pinned old base, not natively** — the resulting
binary's glibc dependency floor is whatever the *build* machine has,
not something `linuxdeploy` can lower after the fact. Building on a
rolling-release host (this project's own dev machine) produced an
AppImage requiring a glibc newer than most real systems have — a real
bug reported and fixed this way, see docs/adr/0045. Ubuntu 22.04 is
the standard choice for Qt6 AppImages: old enough for a low glibc
floor, new enough to have Qt6 as a native package.

```sh
docker run --rm -v "$PWD":/src -w /src ubuntu:22.04 bash -c '
    apt-get update -qq
    apt-get install -y -qq --no-install-recommends \
        cmake g++ qt6-base-dev libgl-dev git ca-certificates curl file build-essential
    bash packaging/appimage/build-appimage.sh
'
```

`libgl-dev` matters even though nothing in this project uses OpenGL
directly — `--no-install-recommends` drops it, and some of Qt6's own
CMake package-config files transitively depend on finding it; without
it, `find_package(Qt6 COMPONENTS Widgets)` fails outright.

## Arch Linux (`makepkg` / AUR)

```sh
cd packaging/arch
makepkg -f
```

Standard `PKGBUILD`, fetches a source tarball from the project's own
GitHub release tag rather than building the local checkout — update
`_tag`/`sha256sums` when cutting a new release. Needs network access
during `build()` too, not just to fetch the source: this project's
own `core/CMakeLists.txt` (Lua) and `modules/syntax/CMakeLists.txt`
(Tree-sitter) both pull dependencies via CMake `FetchContent` at
*configure* time — so this won't build inside a network-isolated clean
chroot yet. Plain `makepkg` works.

## Debian/Ubuntu (`.deb`)

```sh
packaging/debian/build-deb.sh
```

No `debhelper`/`devscripts` dependency — stages an install root with
plain `cmake --install`, writes a `DEBIAN/control` (runtime deps
resolved via `ldd` + `dpkg -S` on the actual linked binary, falling
back to a generic `libqt6widgets6` dependency if `dpkg -S` can't
resolve anything, e.g. built on a non-Debian host), and calls
`dpkg-deb --build` directly. Output lands in `packaging/debian/`.

**Run this inside `debian:bookworm` (current stable), not natively** —
same reasoning as the AppImage above: a `.deb` built on a
rolling-release host declares a `Depends:` floor newer than Debian
stable actually ships, unusable there. See docs/adr/0045.

```sh
docker run --rm -v "$PWD":/src -w /src debian:bookworm bash -c '
    apt-get update -qq
    apt-get install -y -qq --no-install-recommends \
        cmake g++ qt6-base-dev git ca-certificates dpkg-dev
    bash packaging/debian/build-deb.sh
'
```

## Verifying a build without installing it

Every format above stages a normal `usr/`-rooted tree before wrapping
it — `usr/bin/ase_gui` can always be run directly from that staging
directory (AppDir, `pkgroot`, or a `dpkg-deb -x`'d `.deb`) to sanity-
check the result before distributing it.

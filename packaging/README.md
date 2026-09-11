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

## Verifying a build without installing it

Every format above stages a normal `usr/`-rooted tree before wrapping
it — `usr/bin/ase_gui` can always be run directly from that staging
directory (AppDir, `pkgroot`, or a `dpkg-deb -x`'d `.deb`) to sanity-
check the result before distributing it.

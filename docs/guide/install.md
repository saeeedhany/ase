# Install

## A prebuilt package

The alpha ships an AppImage, an Arch `PKGBUILD` and a `.deb` on the
[releases page](https://github.com/saeeedhany/ase/releases/latest). The
AppImage needs nothing installed:

```sh
chmod +x ase-*.AppImage
./ase-*.AppImage
```

Each format is documented in
[`packaging/README.md`](https://github.com/saeeedhany/ase/blob/main/packaging/README.md).

## From source

You need CMake ≥ 3.20, a C11/C++20 compiler, and Qt6 (Widgets). The first
configure fetches Lua, Tree-sitter and its grammars, so it needs network
access; everything is cached afterwards.

```sh
git clone https://github.com/saeeedhany/ase.git
cd ase
cmake -B build -DASE_BUILD_GUI=ON -DASE_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build
```

The binary is `build/gui/ase_gui`. `cmake --install build` puts it on your
path along with a `.desktop` entry and an icon.

On Debian and Ubuntu the Qt dependency is `qt6-base-dev`.

## Headless

The editing engine is a C library with no Qt dependency at all, and can
be built on its own:

```sh
cmake -B build -DASE_BUILD_GUI=OFF
cmake --build build
```

That gives you `core/` — the piece table, undo, config, the LSP client,
the plugin host — without a GUI. See
[ADR 0002](../adr/0002-headless-core-separation.md) for why the split
exists.

`-DASE_BUILD_GUI=ON` requires `-DASE_BUILD_SYNTAX=ON` (the default).
Turning syntax off while keeping the GUI on is not a supported
combination.

## Next

[Your first five minutes](getting-started.md).

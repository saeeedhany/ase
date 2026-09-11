# ADR 0034: Linux packaging (AppImage, Arch PKGBUILD, .deb)

## Status

Accepted

## Context

The project had no `install()` rules at all — nothing to build a
package *from* — and no packaging scripts. User asked to package the
first alpha release for distribution: an AppImage plus "several
packages."

## Decision

### One `install()` rule set, three consumers

`gui/CMakeLists.txt` gained standard freedesktop `install()` rules
(`GNUInstallDirs`): the `ase_gui` binary to `bin/`, a new
`packaging/linux/ase.desktop` to `share/applications/`, and a new
square, padded icon (`packaging/linux/ase-icon-512.png` — the existing
`gui/resources/ase.png` wordmark logo is 1536×1024, not square;
padded via ImageMagick onto a transparent canvas rather than cropped,
so the existing artwork isn't touched or distorted) to
`share/icons/hicolor/512x512/apps/`. Every packaging format below is
just `cmake --install` against this, wrapped differently — nothing
package-specific reimplements "where do the files go."

### AppImage (`packaging/appimage/build-appimage.sh`)

Fetches `linuxdeploy` + `linuxdeploy-plugin-qt` (cached after the
first run), builds Release, stages an AppDir via the install rules
above, runs linuxdeploy to bundle Qt6 and every other shared library
dependency. Two real bugs hit and fixed during actual verification,
not just written and assumed to work:

- `linuxdeploy-plugin-qt` autodetects a `qmake` on `PATH` to identify
  which Qt install to bundle — on Arch (this dev machine has both Qt5
  and Qt6 installed) it found `qmake-qt5` first and failed with
  "Could not find Qt modules to deploy" because the detected major
  version didn't match what `ase_gui` actually links against. Fixed by
  explicitly setting `QMAKE` to the real `qmake6` binary before
  invoking the plugin (`linuxdeploy-plugin-qt` respects that env var).
- linuxdeploy's own bundled `strip` (from its "continuous" build,
  older than this rolling-release distro's binutils) doesn't
  understand a newer ELF section type (`.relr.dyn`, RELR relative
  relocations) and aborted the whole run rather than skipping that one
  file. Fixed with `NO_STRIP=1`, linuxdeploy's own documented escape
  hatch — unstripped bundled libraries just make the AppImage a little
  larger, not a correctness issue.

### Arch Linux (`packaging/arch/PKGBUILD`)

A standard `makepkg` recipe, fetching the source tarball from this
project's own GitHub release tag (`v0.1.0-alpha`) rather than building
the local checkout — this is the shape a real AUR submission needs.
`pkgver` can't contain a hyphen (pacman's own restriction), but the
git tag does (`v0.1.0-alpha`) — `pkgver=0.1.0_alpha` (underscore) is
the pacman-safe rendering, with a separate `_tag`/`_srcdir` pair
spelling out that GitHub's tag tarball extracts to
`ase-0.1.0-alpha` (hyphen, stripped of the leading `v`), which does
*not* equal `$pkgname-$pkgver` — assuming they matched (makepkg's
usual default) would have pointed `build()`/`package()` at a directory
that doesn't exist.

Documented, not solved, limitation: `build()` needs network access
(Lua and Tree-sitter are both fetched via CMake `FetchContent` at
configure time — see `core/CMakeLists.txt`,
`modules/syntax/CMakeLists.txt`), so this won't build inside a
network-isolated clean chroot yet. Vendoring those dependencies is out
of scope here.

### Debian/Ubuntu (`packaging/debian/build-deb.sh`)

Deliberately skips `debhelper`/`devscripts` — stages an install root
with the same `cmake --install`, generates `DEBIAN/control` with
runtime deps resolved from the *actual linked binary* (`ldd` piped
through `dpkg -S`, not a hand-maintained guess), and calls
`dpkg-deb --build` directly. Real bug hit during verification, not
assumed away: this project is developed on Arch, whose `dpkg` has no
package database at all, so `dpkg -S` fails every lookup — and because
the script uses `set -o pipefail`, that failure aborted the *whole
script* before it ever reached the intended fallback
(`DEPENDS="${DEPENDS:-libqt6widgets6}"`). Fixed by isolating the
`dpkg -S` call in its own `{ ... || true; }` group so a failed lookup
degrades to empty output (triggering the fallback) rather than killing
the pipeline.

## Consequences

All three were actually built and run, not just scripted:

- AppImage: built (45MB), launched standalone from `/tmp` (outside the
  dev tree, proving it's genuinely self-contained), opened a test file
  correctly.
- Arch package: `makepkg -f` succeeded against a real download of the
  `v0.1.0-alpha` GitHub tag tarball (`--printsrcinfo` validated too);
  see the note below about why a *second* verification pass was needed
  after this ADR's own changes landed.
- `.deb`: built, `dpkg-deb --contents` confirms the expected file
  layout, `dpkg-deb -x`'d and the extracted binary launched and
  rendered correctly.

One sequencing issue worth recording: the first `makepkg` attempt
against the already-pushed `v0.1.0-alpha` tag produced an empty
package (metadata files only, no binary) — that tag was cut *before*
this ADR's `install()` rules existed, so the fetched source simply had
nothing for `cmake --install` to install. `v0.1.0-alpha` was moved
(force-updated, both locally and on the remote) to include this
commit rather than cutting a new tag, since it had only just been
pushed moments earlier in the same release process with nothing else
depending on it yet — a normal new tag is the right call once a tag
has actually been consumed by anyone else. The PKGBUILD's
`sha256sums` reflects the tarball GitHub serves for the *moved* tag.

Not attempted: Flatpak/Snap (heavier tooling, sandboxing manifests —
not asked for); an `.rpm` (no `rpmbuild` on this machine, not asked
for); vendoring Lua/Tree-sitter so builds work fully offline.

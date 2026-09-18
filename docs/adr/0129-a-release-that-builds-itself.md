# ADR 0129: A release that builds itself

## Status

Accepted

## Context

`v0.4.0-beta` was assembled by hand. Two container builds, a smoke test
of each artifact in two more containers, checksums, then the upload —
about forty minutes of wall clock, most of it waiting.

Doing it once was fine. What made it worth automating is that both of
its failure modes were found **by accident**, not by looking:

**`gh release create` with assets attached is not atomic.** An HTTP 500
partway through the 26MB AppImage upload made `gh` delete the release it
had just created, hand-written notes and all. The release simply was not
there afterwards, and `gh release view` reported "release not found"
rather than anything about an upload.

**GitHub strips `~` from an asset's filename.** `ase_0.4.0~beta1_amd64.deb`
uploaded as `ase_0.4.0.beta1_amd64.deb`, which no longer matched the name
recorded in `SHA256SUMS`. Nobody had downloaded it yet, but
`sha256sum -c` would have failed for everyone who did — a checksum file
that does not verify being worse than none at all.

Neither is discoverable by reading documentation. Both will recur, and
beta means more releases, not fewer.

## Decision

`.github/workflows/release.yml`, on any `v*` tag.

Four jobs. The AppImage is built in `ubuntu:22.04` and the `.deb` in
`debian:bookworm` — the same bases `packaging/README.md` documents, for
the reason [ADR 0045](0045-v0.2.0-alpha-and-containerized-packaging.md)
gives: built on a rolling-release host, they demand a glibc newer than
their targets have.

Both are then run on *clean* containers. An artifact that links is not an
artifact that starts, and the build container is the one place where
"self-contained" cannot be tested, because everything it needs is already
installed there.

The two failures above are encoded rather than remembered. The release is
created first and assets uploaded separately, each retried three times.
The `.deb` is renamed away from `~` before checksums are computed, so
`SHA256SUMS` describes what a person actually downloads; the package's own
`Version:` field keeps the tilde, which is what `apt` orders on.

A version in an artifact that disagrees with the tag fails the run,
because a release that ships something built from another commit is worse
than no release.

### It stops at a draft

CI can prove the artifacts build, start, and match their checksums. It
cannot decide that a tag was meant to be a release, and `git push --tags`
is one keystroke from an accident. The generated notes are a starting
point; the real ones are written by a person, who then publishes.

## Consequences

`workflow_dispatch` takes a tag and a `dry_run` flag that defaults to
true, skipping the draft job entirely. That is how this workflow was
tested — against `v0.4.0-beta`, which already had a published release
whose checksums a rebuild would otherwise have replaced.

The AppImage smoke test installs `libqt6widgets6` rather than testing on
a bare container, and that is deliberate. An AppImage is not fully
self-contained by design: linuxdeploy's excludelist omits the font stack
— harfbuzz, freetype, fontconfig — because bundling those breaks
rendering on a host whose fontconfig differs. The bundle confirms it:
`libgraphite2`, which is *not* on that list, is included, while harfbuzz,
which depends on it, is not. So the honest test is "does it run on a
desktop", which is what an AppImage promises, rather than "does it run on
nothing", which it never did. The shipped `v0.3.0-alpha` AppImage has the
same 47 libraries and the same gap.

### Its first run failed, correctly

Dispatched as a dry run against `v0.4.0-beta`, the version check
rejected the artifacts it had just built:

```
Absolute_Simple_Editor-0.4.0-alpha-x86_64.AppImage does not carry 0.4.0-beta
installed 0.4.0~alpha1
```

Both artifacts built and both ran. They were labelled **alpha** because
`v0.4.0-beta` points at the commit that bumped the version but *not* at
the one that fixed the packaging scripts — which still wrote the stage
out by hand beside the number they read from `CMakeLists.txt`. That fix
landed one commit later.

So the published `v0.4.0-beta` artifacts, which do say beta, were built
from a working tree rather than from the tag, and the tag does not
reproduce them. Nobody would have noticed until someone tried to rebuild
a release they did not trust — which is exactly when it matters most.

The automation found, on its first run, a defect in the release it was
written because of.

## What is still manual

Writing the notes, publishing the draft, and updating
`packaging/arch/PKGBUILD`, which pins the tarball's hash and so can only
be correct after the tag exists.

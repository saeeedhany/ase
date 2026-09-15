# ADR 0095: The performance baseline

## Status

Accepted

## Context

[ADR 0089](0089-closing-a-buffer.md) through
[ADR 0094](0094-what-startup-actually-costs.md) each fixed something
slow, and each recorded only the numbers that justified its own change.
A great deal more was measured along the way and written down nowhere:
what typing costs, what the background timers cost, whether `:compile`
blocks, how memory grows with tabs.

That is a shame twice over. The unrecorded numbers are the ones that say
*nothing is wrong here* — and "we already checked, here is the figure" is
exactly what stops the next person re-deriving it or, worse, optimising
something that was never the problem. Three separate times in this line
of work a plausible-looking suspect turned out to cost under a
millisecond.

This ADR is a reference, not a decision. It records the whole sweep.

## Decision

### Environment

Every figure below: AMD Ryzen 7 7735HS (16 cores), 15 GB RAM, Qt 6.11.2,
GCC 16, clangd 22.1.8, `CMAKE_BUILD_TYPE=Release`. GUI driven headlessly
through Qt's `vnc`/`offscreen` platform plugins. Files referred to by
size: **11 KB** (`core/src/buffer.c`), **60 KB**
(`gui/src/editor_viewport_vim.cpp`), **277 KB** (a generated 12,600-line
C file). Numbers are from a single machine and are for *shape and
proportion*, not absolute promises.

### What a keystroke costs

| operation | cost |
|---|---|
| typing, per keystroke — 277 KB | 6.8 ms (5.5 refreshCache + 1.3 paint) |
| typing, per keystroke — 60 KB | ~3 ms |
| paint, one frame | 1.1–4.2 ms |
| `Ctrl+A` select all, `G`, `gg` | 0.6–1.1 ms |
| `Ctrl+Tab` between buffers | 0.8–1.2 ms |
| first switch *to* a buffer | 1.6 ms |
| jumplist `Ctrl+O` / `Ctrl+I` | 1.6–1.7 ms |
| `Ctrl+F` open find | 1.6 ms |
| `recomputeMatches` (find open) | 0.8 ms |
| `Ctrl+S` save, `Ctrl+C`, `Ctrl+N` | < 0.5 ms |
| close a buffer with a server attached | 0.03 ms |

Typing on the 277 KB file breaks down as: Tree-sitter incremental parse
3.1 ms, LSP full-document sync 1.2 ms, buffer copy and line scan
0.22 ms, capture fill 0.02 ms. **The parse is the cost**; the
whole-buffer copy that looked like the obvious suspect is 3% of it.

Within that parse: `derive_edit` 0.18 ms, `ts_parser_parse_string`
3.1 ms, text snapshot 0.20 ms, query over the visible range 0.55 ms.

### What bulk operations cost

These are the only things over 10 ms, and each is work the user asked
for explicitly:

| operation | cost |
|---|---|
| paste 277 KB over a 277 KB buffer | 78 ms |
| replace all — 1575 matches in 277 KB | 104–107 ms |
| undo a 277 KB paste | 17 ms |
| `Alt+O` file browser | 6.4 ms |
| `Ctrl+P` file finder — 330 files | 4.5 ms |
| `Ctrl+P` — 10,148 files | 44.9 ms |
| window resize (relayout + repaint) | 1.4–6.3 ms |
| `:compile` spawn | 2.0–2.6 ms |

`Ctrl+P` scales at roughly **4.4 ms per 1000 files** and the walk is
capped at 20,000, so a large tree costs ~88 ms. It is not cached. Fine
at 330 files, worth revisiting for anyone working in a monorepo.

Resize does **not** scale with file size — only the visible range is laid
out, so 277 KB resizes exactly like 11 KB.

`:compile` spawns in ~2 ms and does not block: measured with `gcc -c` on
the 277 KB file taking **602 ms**, four `j` presses sent 150 ms in were
all processed, and the cursor ended where they put it.

### What runs when you are not looking

| | |
|---|---|
| `pollLsp` — every 200 ms | 0.68 ms |
| `checkConfigReload` — every 750 ms | < 0.3 ms |
| idle CPU, `animations = true` | 6.0% of one core |
| scroll frame interval | matches the display (16.1 ms at 60 Hz) |

`checkConfigReload` matters because
[ADR 0087](0087-project-config-says-what-files-mean.md) added a walk up
the directory tree to it, looking for `.ase.conf`. It stays under the
0.3 ms probe floor, so that addition is as cheap as it was claimed to be.

The 6% idle figure is a full-viewport repaint to advance a caret fade —
still the known waste named in
[ADR 0090](0090-scrolling-runs-at-frame-rate.md).

### Startup and memory

| phase | 11 KB | 277 KB |
|---|---|---|
| `QApplication` | 1.7 ms | 1.6 ms |
| read the file | 0.3 ms | 0.4 ms |
| `MainWindow` | 19 ms | 15 ms |
| `addBuffer` | 19 ms | 113 ms |
| show + event loop | 0.7 ms | 0.7 ms |
| **to event loop** | **43 ms** | **98 ms** |
| **to idle** (11/60/277 KB) | 81 / 101 / 121 ms | |

A bare Qt6 `QMainWindow` with a status bar reaches its event loop in
**13.7 ms** here. That is the floor. Inside `addBuffer`, the 277 KB cost
is `refreshCache` at 97 ms — the initial full parse, which cannot be
incremental and cannot be limited to the visible range.

Memory: **40.7 MB** with one 11 KB buffer, **43.2 MB** with five mixed
buffers — about **0.6 MB per additional tab**.

### Language-server round trips

Asynchronous, so these are latency-to-result, not UI stalls:
completion **13–152 ms**, go-to-definition **3.1 ms**. The upper end of
completion is clangd thinking about a 60 KB C++ file; the editor stays
responsive throughout and the popup simply arrives when it arrives.

## Consequences

**Nothing in this table is currently worth optimising.** Every
single-keystroke path is under 5 ms, every operation over 10 ms is
explicit bulk work dominated by a reparse that has to happen, and
everything long-running is off the UI thread. The next performance
complaint should be measured against these figures before anything is
changed, because four of the five real problems found in this line of
work were invisible in the code and obvious in a measurement.

**Three methodology traps, all of which produced a wrong answer before
being caught**, and all of which will catch the next person:

- **Measure in the right directory.** `Ctrl+P` was reported at 50 ms
  because the test file lived in a scratch directory holding ~10,000
  generated files, and `project::rootFor` found no `.git` above it. In
  the actual repository it is 4.5 ms. A count of 10,148 files in a repo
  with 228 tracked files should have been implausible on sight.
- **Measure the state you mean.** Closing a file with a language server
  was declared fixed at 19 ms in
  [ADR 0089](0089-closing-a-buffer.md), measured against an *idle*
  clangd. Against one still indexing — which is what happens when you
  open a file and close it soon after, the actual reported complaint —
  it was 241 ms.
- **Do not store a pointer into a temporary.** The probe harness kept
  `QKeySequence(...).toUtf8().constData()` as its label and printed
  freed memory. The first sweep produced a table of garbage names.

**What is not measured here:** multi-monitor behaviour, anything under
memory pressure or on spinning disks, files above 277 KB, and any
platform other than Linux. `refreshCache` grows with file size, so a
multi-megabyte file will be worse than anything recorded above — nobody
has tried one.

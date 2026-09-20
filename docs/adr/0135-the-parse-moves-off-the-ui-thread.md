# ADR 0135: The parse moves off the UI thread

## Status

Accepted

## Context

Tree-sitter parses the whole file to build a tree however little of it is
on screen. [ADR 0107](0107-the-large-file-wall.md) measured it — 230ms at
1MB, 1.07s at 4.5MB — and answered with two retreats, because the parse
was on the thread that draws:

- past 256KB, wait for a pause in typing, so a large file was **plain
  while you typed**;
- past `syntax_max_kb`, do not parse at all, so a large file had **no
  colours ever** and said so in the status bar.

Both were the better of two bad answers. [ADR 0124](0124-the-debounce-that-never-happened.md)
later found the first one had never even worked.

There is a third answer, which is to do the same parse somewhere that is
not drawing.

## Decision

A `SyntaxWorker` on a `QThread`, for files above the same 256KB
threshold. Below it, nothing changes: the parse is immediate and on this
thread, because a 13KB file's colours arriving a frame late would be a
regression rather than a fix.

**Exactly one of the two is ever live.** `rebuildSyntax()` picks by size:
either `m_syntax` exists and the UI thread parses, or a worker exists and
the UI thread never touches a parser at all. There is no mode where both
could run.

### What is shared, and why that is the whole argument

The worker owns its `AseSyntax` — created on its own thread, on first
parse, because `moveToThread` moves the object and a parser built here
for another thread to use is exactly the bug this is meant not to have.

The only thing crossing the boundary is the text, as a `QByteArray`.
Implicit sharing means handing one over copies a pointer and bumps an
atomic refcount rather than copying a megabyte, and neither side mutates
what the other holds: the worker only reads, and the next edit reassigns
`m_cache`, leaving the worker holding what it was given.

Everything else is a queued signal.

### One request in flight

Typing faster than the parser would otherwise queue a parse per
keystroke, each already stale before it started. Instead the latest
wanted window is remembered and asked for when the worker comes back.

A result carries the version it was asked with. If the buffer has changed
since, it is dropped — and because the pump then re-asks, the answer
converges on the current text rather than stopping at a stale one.

### No debounce any more

The 40ms timer existed to keep the parse off the keystroke. The parse is
not on the keystroke now, so the timer is gone. Colours arrive as fast as
the parser can produce them, including mid-burst.

`m_highlightDeferred` survives with a narrower meaning: the window is
empty because a worker is filling it. The
[ADR 0124](0124-the-debounce-that-never-happened.md) invariant still
holds — while it is empty the text draws plain and the caret is measured
plain, so the two agree.

### Letting go without waiting

Stopping the worker was `quit()` then `wait()`, which is the obvious
thing and cost exactly what the worker was supposed to save: closing a
603KB buffer blocked for **125ms**, and a 4MB one would have blocked for
about a second — a freeze on close instead of a freeze on open. The test
written to prove opening was fast is what caught it, by measuring a
scope that included the destructor.

There is nothing to wait for. The worker owns its parser, and the text it
holds keeps itself alive. Disconnecting first means the result it is
midway through is not delivered, which is what dropping it would do
anyway. Both objects delete themselves when the loop stops. Open and
close of that same buffer is now **11ms**.

## Consequences

The size cap stays, for a different reason. Parsing is no longer a
question of time on this thread, so what is left to bound is memory —
measured at about 26× the source: the same 2MB file is 61MB resident
unparsed and 114MB parsed. The default rises from 1024KB to 4096KB, and
its description now says what it is protecting.

A 1.9MB file that used to open with no colours and a notice saying so now
opens coloured, and stays coloured while being typed into.

Verified four ways, because this is the riskiest change in the codebase:

- the 220 vim conformance cases and all 20 suites still pass;
- two tests drive a real worker through the event loop, one of them
  typing *during* the first parse to exercise the stale-version path;
  both fail if the result is never delivered;
- ThreadSanitizer reports no race whose stack shows this code touching
  shared data. Its three findings are libglib's eventfd — Qt's
  queued-connection wakeup, which TSan does not model as synchronisation
  and reports as a race on a *file descriptor* — and Qt Test's own
  watchdog. That is weak evidence on its own, since Qt here is not
  instrumented, which is why the ownership argument above is the real
  one;
- and the editor itself, on a 1.9MB file.

What is not addressed: the first parse of a very large file still takes
as long as it takes, so a 4MB file is plain for about a second after
opening. It is responsive throughout, which is the difference, but it is
not instant.

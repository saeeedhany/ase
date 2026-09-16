# ADR 0111: Session restore

## Status

Accepted

## Context

Reopening a project meant reopening every file in it by hand, and
scrolling back to where you were. The roadmap has listed this under
"table stakes of a daily driver" since it was written.

## Decision

### A file beside `config.ase`, in the same shape

`<config dir>/session.ase` records the open files, the caret offset and
scroll line in each, and which one was in front:

```
ASE-SESSION 1
active = 2
file = /home/u/one.c
cursor = 42
scroll = 3
```

`file` opens an entry and `cursor`/`scroll` fill in the one most
recently opened, so a value arriving before any `file` is ignored rather
than misfiled. Everything after `file = ` is the path, `=` and spaces
included, so only a path containing a newline cannot be represented —
such a path is left out rather than written as something that reads back
wrong.

No text is stored. The files are on disk, and anything unsaved is
ADR 0110's problem.

### Only when started bare

`ase foo.c` means foo.c, not "and the eleven things I had open last
week". Launching with no argument is the only case that restores.

The session then records whatever ends up open, including after a
launch with an argument — the session is "what was open", with no
special cases. That does mean opening a single file and quitting leaves
a one-file session. `restore_session = false` turns the whole thing off.

### Written whenever the buffer list changes, and every five seconds

Buffer changes write immediately. The caret moving is far too hot to
write on, so a five-second timer catches it — and writes nothing unless
something actually moved, so an idle editor does no I/O. A `SIGTERM`, a
logout or a crash then costs at most a few seconds of scroll position
rather than the whole session.

Writing only in `closeEvent` was the first attempt and it is wrong:
`SIGTERM` does not run it, which the end-to-end test found immediately
by terminating the editor and getting no session file at all.

### Missing files are skipped

A session should not refuse to start because a file was deleted, nor
resurrect one that no longer exists. If nothing survives, the editor
falls back to the usual empty buffer and its welcome greeting.

## Consequences

Twelve core tests, six of which were checked by mutation: removing the
header check, accepting trailing junk in a number, writing a path with a
newline, and misfiling `cursor`/`scroll` onto the wrong entry are each
caught.

Two of those tests were written wrong the first time and passed against
a broken implementation. `test_headerless_file_is_rejected` fed a file
whose first line was the `file =` entry, which the loader consumes as a
header line either way — so with the check removed the result was still
"nothing to restore". Fixed by giving the junk file a second line. The
lesson is the same one the recovery suite gave: a test that passes
immediately is a claim, not evidence.

Restoring the caret is queued rather than done inline, because
`addBuffer` runs before the window is shown and `ensureCursorVisible`
would scroll against a viewport with no real height yet. Without that,
the scroll came back as 22 where 17 was saved.

Unnamed buffers are not recorded, since there is nothing to reopen.

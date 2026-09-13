# ADR 0062: The status bar message line

## Status

Accepted

## Context

The editor had no way to say anything. An unconfigured language server,
a `:` command that doesn't exist, a plugin that failed to load, a write
that failed — all of it was *silence*, which reads as "broken" rather
than as "not set up". That is exactly how an external tester concluded
LSP didn't work in the packaged builds.

## Decision

### Two problems, not one

Worth separating before building anything, because conflating them is
how editors end up with toast spam.

**State** is continuously true or false — "a language server is
running", "a build is in flight", "you are in Normal mode". The answer
is wanted at a glance, at any moment. A message is the wrong shape for
it: it fires once and is gone, so anyone who looked away can never find
out. **The tester's complaint was this kind** — they didn't miss a
message, there was nothing to look at.

**Events** happen at an instant and are stale a second later: saved,
no matches, config reloaded, command not found.

This ADR builds the second. State gets a persistent status-bar segment,
next.

### One funnel

Everything the editor says goes through `EditorViewport::notify(level,
text)`, which emits `messagePosted`; `MainWindow` owns the rendering.
One place decides what a message looks like.

This is the single most important decision here, and it is not about
this feature: three ways of telling the user something becomes six
within a month, all slightly different. That is precisely what happened
with animation durations before ADR 0053 consolidated them, and the fix
was more expensive than the rule would have been.

`notify()` is public, so panels — and eventually plugins — can reach it.
A plugin that cannot say "that file isn't a thing" is a plugin that
fails silently.

### The status bar, not toasts

The message occupies the free middle of the status bar, immediately
right of the mode label. Mode and `Ln/Col` keep their places and never
move.

Floating toasts were rejected: they cover the text you are reading, they
stack, and they are the most "app-like" chrome there is — in a project
whose entire aesthetic is no lines, no boxes, state carried by opacity
(ADR 0007, ADR 0022). The status bar is already there, already the place
you look for state, and is what vim itself uses, in an editor that
defaults to Vim mode.

Messages replace each other rather than queueing. A queue means being
told about something that happened several seconds ago, which is worse
than missing it.

### Three tiers, separated by dwell — not by colour

| | rendering | dwell |
|---|---|---|
| Info | text colour, one opacity tier down | 3s |
| Warning | full-strength text colour | 6s |
| Error | `diagnostic_error` | until you do something else |

Info drops a tier because a confirmation you did not ask for should not
shout as loudly as the mode label beside it — the same "secondary lines
drop one tier" move the About panel and welcome screen already make
(ADR 0055). A warning is a sentence you are meant to finish reading,
usually saying the thing you just asked for did not happen, so it does
not drop and it stays twice as long.

Errors reuse the one non-monochrome colour the theme already sanctions,
the same `diagnostic_error` as the gutter dot and the underline. No new
hue enters the palette.

### An error stays until you do something else

Vim's rule, and the reasoning is not aesthetic: **the one message you
must not miss is the one saying a thing you asked for did not happen.**
A save that failed while you were looking at your other monitor is
exactly how people lose work. So errors have no timeout.

"Doing something else" is any cursor move or edit. `statusChanged`
already fires on exactly those and on nothing that isn't the user
(ADR 0023), so this needs no new signal and cannot miss a case a new one
would. Sticky dismissal is armed one event-loop turn *after* the message
appears, so the keystroke that caused the error cannot also dismiss it
before it has been seen.

Switching buffers clears the message too: one raised by the file you
just left, sitting in the bar above a different file's text, reads as
being about this one.

### Long text elides, rather than pushing the readout off the edge

A message with a long path elides in the middle, with the full string on
hover. The alternative — letting the label grow — pushes `Ln/Col` off a
narrow window, trading a permanent readout for a transient one.

## Consequences

Five call sites wired, chosen so the thing is judgeable rather than
theoretical: save success, save failure, unknown `:` command, no search
matches, and config hot-reload.

Two of those were silent bugs rather than missing features:

- **A failed write was indistinguishable from a successful one.** The
  dirty marker simply stayed. You found out that your file wasn't on
  disk by noticing an asterisk that should have cleared.
- **A typo in a `:` command looked exactly like a command that ran and
  did nothing** — ADR 0025's deliberate silent no-op, which was the
  right call when there was nowhere to say otherwise.

Search reports "no matches" on Enter only, never while typing:
incremental search passes through "no matches" on the way to almost
every real query, and a message per keystroke is noise.

Verified live: `saved notif.c` renders one tier below the mode label
(192 vs 225 measured) and is gone by 4s; `unknown command: frobnicate`
sits at full strength; `could not write ro.txt` renders red, survives
several seconds untouched, and clears the instant `j` moves the cursor;
switching tabs clears a message raised by the other buffer.

`ctest` 9/9, clean build, zero warnings.

Deliberately not built here:

- **`:messages` history**, so a message you missed is recoverable. The
  `OutputPanel` already exists as a docked, non-modal, streaming surface
  and should grow a second mode rather than a new widget being invented.
- **The persistent LSP state segment** — the actual fix for the tester's
  report, and state rather than an event.
- **Desktop notifications.** Wrong layer, new dependency, and an editor
  interrupting your desktop to say "saved" is obnoxious.

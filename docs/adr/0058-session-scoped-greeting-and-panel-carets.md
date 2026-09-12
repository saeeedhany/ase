# ADR 0058: The greeting belongs to the session, and panel carets join the app

## Status

Accepted

## Context

Two pieces of feedback, both about a thing being *almost* right: the
welcome screen appearing in one situation too many, and the caret
changing character the moment you opened a panel.

## Decision

### The welcome screen greets the session, not the buffer

ADR 0057 narrowed it from "the buffer is empty" to "this buffer started
with no path", which fixed the file cases but left one: `Ctrl+N` (and
the tab strip's `+`) makes a pathless buffer too, so a new tab opened an
hour into a session was greeted with the logo, the version and a list of
first-steps shortcuts. You are already editing by then. The greeting is
for the moment you have nothing open at all.

A viewport genuinely cannot decide this for itself — a `Ctrl+N` buffer
and the startup buffer are byte-for-byte identical from inside. What
separates them is *when* they were made, which only the window knows.
So `m_welcomeEligible` is no longer inferred in the constructor; it is
armed from outside by `armWelcomeGreeting()`, called from `main()` for
the startup buffer and only when `argc == 1` — the one place that
actually knows the editor was launched with no file.

The first-keystroke latch from ADR 0057 is unchanged, and still the
thing that makes this a *greeting*.

One case left deliberately as-is: switching back to an untouched startup
buffer still shows it. That buffer *is* the empty editor you launched,
nothing has been typed into it, and blanking it on return would mean
tracking "has been looked away from", which is a state nobody asked for.

### Panel text fields get the editor's caret

Every field in the panels — Find and Replace, Open/Save-As, the `:`
command line, the shortcut search — was a plain `QLineEdit`, so the
caret changed character as soon as you opened one: the editor's glides
between positions and fades on a cosine breathe cycle, Qt's native one
hard-blinks in place. ADR 0028 matched their *rate* through
`QApplication::setCursorFlashTime`, and said in as many words that
matching the rest would mean replacing the native caret painting, left
for if the rate match alone stopped being enough. It did.

`SmoothLineEdit` is that replacement. It reuses the editor's own two
mechanisms rather than approximating them: `motion::kEaseFactor` per
`motion::kTickMs` tick for the glide (the same exponential decay, so a
caret crossing a field decelerates exactly like one crossing a line of
code), and the same `128 + 127·cos` breathe on the same 24-tick cycle.
With `animations = false` it falls back to the same hard blink the
editor uses in that mode, pushed in by each panel's `refreshTheme()` so
a config hot-reload reaches it.

Two implementation notes worth keeping, because both were the difference
between this working and not:

- **The native caret is removed through the style.** `QLineEdit` has no
  public `setCursorWidth()` (`QPlainTextEdit` does; `QLineEdit` does
  not), and painting over the caret would mean re-rendering the text
  under it. But it takes its caret width from
  `QStyle::PM_TextCursorWidth`, so a `QProxyStyle` reporting `0` for
  that one metric removes the caret and touches nothing else — text,
  selection, placeholder and frame painting all stay Qt's.
- **The caret position comes from `QLineEdit::cursorRect()`**, so
  horizontal scrolling in an overflowing field, margins and alignment
  stay Qt's problem. That rect is padded for repaint purposes; the caret
  sits `ceil((width - cursorWidth) / 2)` from its left edge, derived
  from the rect rather than hardcoded, so a change to that padding
  doesn't silently shift every caret in the app by five pixels.

The timer runs only while a field has focus. Five fields across five
panels per buffer, all ticking 33 times a second forever, is exactly the
kind of idle cost ADR 0053 went and removed.

## Consequences

Verified live, by sampling the caret column across consecutive frames
rather than by looking at one screenshot:

- Launching bare shows the greeting; `Ctrl+N` from there opens a second
  `untitled` with no greeting; launching on a file shows none.
- In all four field kinds (Find, Open, command line, shortcut search) a
  2px caret sits at the right character offset and cycles smoothly —
  measured brightness 181 → 117 → 77 → (trough) → 63 → 117 over
  successive frames, the cosine curve, not a toggle.
- Backspace catches the caret mid-glide one column short of its
  destination before it lands.
- With `animations = false` the same caret reads 225 or nothing, never
  an intermediate value — a hard blink, as intended.

`ctest` 9/9, clean build, zero warnings.

Not done: `QTextEdit`/`QPlainTextEdit` fields elsewhere in the app (the
output panel, the hover and help bodies) still use Qt's caret. They are
read-only, so they show no caret at all, and the question never arises.

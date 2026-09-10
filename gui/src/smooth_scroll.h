#ifndef ASE_SMOOTH_SCROLL_H
#define ASE_SMOOTH_SCROLL_H

class QAbstractScrollArea;
class EditorViewport;

/* App-wide smooth-scroll behavior — see docs/adr/0031. Installs a
 * wheel-event filter on `area` (works on QScrollArea, QListWidget,
 * QPlainTextEdit, or anything else deriving from QAbstractScrollArea)
 * that eases its vertical scrollbar toward each wheel notch's target
 * instead of jumping, matching the main editor's own scroll glide.
 * Reads `viewport->animationsEnabled()` fresh on every wheel event —
 * the same live value every panel's own refreshTheme() already
 * re-checks — so there's nothing to push in on a config reload. One
 * call per widget, ever: any future scrollable panel gets this for
 * free instead of reimplementing per-widget scroll animation. */
void installSmoothScroll(QAbstractScrollArea *area, EditorViewport *viewport);

#endif /* ASE_SMOOTH_SCROLL_H */

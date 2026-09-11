#ifndef ASE_EDITOR_VIEWPORT_H
#define ASE_EDITOR_VIEWPORT_H

#include <QByteArray>
#include <QColor>
#include <QDateTime>
#include <QFont>
#include <QFontMetrics>
#include <QPointF>
#include <QString>
#include <QVector>
#include <QWidget>

extern "C" {
#include "ase/buffer.h"
#include "ase/config.h"
#include "ase/lsp_client.h"
#include "ase/process.h"
#include "ase/syntax.h"
#include "ase/undo.h"
}

/* GUI-side copy of one AseLspDiagnostic — the core struct's `message`
 * is a borrowed pointer, only valid during the diagnostics callback,
 * so it can't be stored as-is. See docs/adr/0029. */
struct GuiDiagnostic {
    int startLine;
    int startChar;
    int endLine;
    int endChar;
    int severity; /* 1=Error, 2=Warning, 3=Information, 4=Hint — per LSP */
    QString message;
};

/* Animated focus state for one diagnostic line — see docs/adr/0041.
 * `reveal` is the current eased 0..1 value (wipe fraction across the
 * diagnostic underline's own span, brightness fraction for the gutter
 * dot); `target` is 1 while the cursor sits on `line`, else 0. Entries
 * are removed once settled at target 0 — see updateDiagnosticLineHighlights(). */
struct DiagnosticLineHighlight {
    int line = -1;
    double reveal = 0.0;
    double target = 0.0;
};

/* GUI-side, decoupled from CompletionPopup::Item the same way
 * GuiDiagnostic is decoupled from AseLspDiagnostic — parsed straight
 * out of the raw completion JSON in applyCompletionResult(), then
 * converted to CompletionPopup::Item right before handing them to the
 * popup. See docs/adr/0030. */
struct GuiCompletionItem {
    QString label;
    QString insertText;
    QString detail;
};

class QTimer;
class QPainter;
class FindBar;
class FileBrowserPanel;
class CommandLine;
class OutputPanel;
class HelpPanel;
class AboutPanel;
class CompletionPopup;
class HoverPanel;

/*
 * Custom-painted text viewport: fills the whole window, no chrome of its
 * own. Owns the AseBuffer it renders — see docs/adr/0002 for why this
 * class, not the core, holds Qt-specific state. Known v1 shortcuts (full-
 * buffer mirroring, byte-level cursor, no IME) are documented in
 * docs/adr/0006, not repeated here. Multi-cursor, the opt-in caret-fade
 * animation, and the accessibility pass are documented in docs/adr/0012.
 * The keyboard/mouse selection model is documented in docs/adr/0019.
 * The find/replace bar (gui/src/find_bar.h) is documented in
 * docs/adr/0021 — this class owns the actual search/replace logic,
 * FindBar is just the input widget calling into it. Status bar, dirty
 * tracking, and Open/Save-As (gui/src/file_browser_panel.h) are
 * documented in docs/adr/0023 — same "this class owns the logic, the
 * FloatingPanel is just the input widget" split. The command line and
 * :compile (gui/src/command_line.h, gui/src/output_panel.h) are
 * documented in docs/adr/0025, same split again — OutputPanel is the
 * one exception to "FloatingPanel": it's a docked panel, not a
 * centered overlay, since you want to watch it stream while still
 * looking at your code, not glance-act-dismiss it.
 */
class EditorViewport : public QWidget {
    Q_OBJECT

public:
    EditorViewport(AseBuffer *buffer, QString filePath, QWidget *parent = nullptr);
    ~EditorViewport() override;

    /* Wires this viewport to the FindBar/FileBrowserPanel instances
     * floating over it (see main.cpp) — plain pointers, not signal/slot
     * connections (neither declares its own Qt signals; they call back
     * into this class's public methods directly). */
    void setFindBar(FindBar *bar) { m_findBar = bar; }
    void setFileBrowser(FileBrowserPanel *panel) { m_fileBrowser = panel; }
    void setCommandLine(CommandLine *panel) { m_commandLine = panel; }
    /* Not a FloatingPanel (see the class comment) — still wired the
     * same plain-pointer way. */
    void setOutputPanel(OutputPanel *panel) { m_outputPanel = panel; }
    void setHelpPanel(HelpPanel *panel) { m_helpPanel = panel; }
    void setAboutPanel(AboutPanel *panel) { m_aboutPanel = panel; }
    void setCompletionPopup(CompletionPopup *popup) { m_completionPopup = popup; }
    void setHoverPanel(HoverPanel *panel) { m_hoverPanel = panel; }

    /* Called by CommandLine on Enter — see docs/adr/0025. `:w`/`:q`/
     * `:compile`/`:output` (toggles the output panel); anything else is
     * a silent no-op, matching the plugin host's existing "skip, don't
     * crash" tolerance (ADR 0009) rather than an error message for a
     * typo. */
    void runCommand(const QString &command);

    QString filePath() const { return m_filePath; }
    /* Destroys the current buffer/syntax/undo-history and loads `path`
     * fresh — same "missing/unreadable file starts empty, path becomes
     * the save target" tolerance ase_buffer_create_from_file's caller
     * in main.cpp already had (docs/adr/0006), not a new behavior.
     * Called by FileBrowserPanel on Ctrl+O. */
    void openFile(const QString &path);
    /* Sets m_filePath then goes through the normal save() path (so
     * dirty-clearing and the statusChanged emit happen exactly once,
     * not duplicated here). Called by FileBrowserPanel on Ctrl+Shift+S. */
    void saveAs(const QString &path);

    /* Theme accessors for FloatingPanel-based chrome (FindBar today,
     * more later) — see docs/adr/0022. That chrome has no AseConfig
     * access of its own, so it always reaches colors/settings through
     * these rather than duplicating config parsing. */
    QColor backgroundColor() const { return m_backgroundColor; }
    QColor textColor() const { return m_textColor; }
    QColor selectionColor() const { return m_selectionColor; }
    QColor panelBackgroundColor() const { return m_panelBackgroundColor; }
    /* Derived, not configured — a low-alpha tint of the text color, so
     * a floating panel's border never needs its own config key. */
    QColor panelBorderColor() const {
        QColor c = m_textColor;
        c.setAlpha(60);
        return c;
    }
    /* Derived: a lightened, fully opaque variant of the background —
     * lets an input field inside a floating panel read as a distinct
     * control without introducing a new hue. */
    QColor panelFieldColor() const {
        QColor c = m_backgroundColor;
        c.setAlpha(255);
        return c.lighter(130);
    }
    bool animationsEnabled() const { return m_animationsEnabled; }

    /* Called by FindBar; see docs/adr/0021. */
    QString primarySelectionText() const;
    void setFindQuery(const QString &needle);
    void clearFindQuery();
    void findNext();
    void findPrevious();
    void replaceCurrentMatch(const QByteArray &replacement);
    void replaceAllMatches(const QByteArray &replacement);

    /* Public only so the AseLspClient diagnostics-callback trampoline (a
     * free function in editor_viewport.cpp — C callbacks can't be member
     * functions) can reach it; not meant to be called from elsewhere.
     * Copies the borrowed AseLspDiagnostic array into m_diagnostics and
     * repaints. See docs/adr/0029. */
    void applyLspDiagnostics(const char *uri, const AseLspDiagnostic *diagnostics, size_t count);
    /* Same "public only for the trampoline" reasoning as
     * applyLspDiagnostics above — see docs/adr/0030. */
    void applyLspCompletion(const AseJsonValue *result, const char *error_message);
    void applyLspHover(const AseJsonValue *result, const char *error_message);

signals:
    /* Emitted from ensureCursorVisible() — every call site that already
     * calls it (every cursor move and every edit) gets this for free,
     * rather than annotating each one individually. 1-based line/column
     * for display. See docs/adr/0023. */
    void statusChanged(int line, int column, bool dirty);

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    void loadConfig();
    void applyConfig();
    void checkConfigReload();
    void refreshCache();
    void drawLine(QPainter &painter, int start, int end, int y);
    QFont fontForCapture(AseHighlightCapture capture) const;
    QColor colorForCapture(AseHighlightCapture capture) const;
    /* Cached per applyConfig() call, not reconstructed per run per paint
     * — QFontMetrics construction isn't free, and drawLine/xForColumn
     * were doing it for every styled run on every visible line, every
     * frame. See docs/adr/0017. */
    const QFontMetrics &metricsForCapture(AseHighlightCapture capture) const;
    QVector<AseHighlightCapture> capturesForLine(int start, int end) const;
    /* Exact pixel x of `column` within [lineStart, lineEnd), measured the
     * same way drawLine actually renders (per-run, with that run's real
     * font) rather than assumed via column * m_charWidth — see
     * docs/adr/0013's caret-drift fix. */
    int xForColumn(int lineStart, int lineEnd, int column) const;
    /* Inverse of xForColumn: which column's rendered glyph a local x
     * coordinate falls nearest to, measured the same per-run way — see
     * docs/adr/0039's fix for the mouse-side counterpart of the
     * docs/adr/0013 caret-drift bug. */
    int columnForX(int lineStart, int lineEnd, int localX) const;

    /* 0 when line numbers are off; otherwise measured (not assumed —
     * see docs/adr/0014) from the widest line-number string actually
     * needed. */
    int gutterWidth() const;
    QString gutterLabelForLine(int line, int cursorLine) const;

    /* Eases m_renderedScrollLine/X and m_renderedCaretPos toward their
     * logical targets (m_scrollLine/X, m_cursors) by one step. Called
     * from the top of paintEvent, not the timer, so it's never stale
     * relative to what's about to be drawn — see docs/adr/0015. When
     * animations are off, snaps rendered state to the target instead
     * of easing (today's instant behavior, preserved). */
    void updateAnimation();
    /* Where cursor's caret should render right now, in widget pixel
     * space, given the *current* (possibly still-easing) scroll
     * position — not its final settled position. */
    QPointF caretTargetFor(size_t cursor) const;
    /* Forces the caret solid-visible and restarts the idle countdown
     * that both the hard blink's toggle and the animated fade's phase
     * are measured from — call on every cursor-moving action (key or
     * mouse) so the caret never blinks/fades away mid-use, in either
     * mode, and only resumes once activity actually stops. See
     * docs/adr/0016 and docs/adr/0017. */
    void resetCaretBlink();
    /* Forces m_renderedScrollLine/X and every m_renderedCaretPos entry
     * to their exact logical target, bypassing the easing in
     * updateAnimation() for this one update. Called after a text edit
     * (typing/deleting), even with animations on — gliding to keep up
     * with fast, repeated small jumps just shows up as a caret that
     * can't keep pace with typing, which is a "the editor is slow"
     * feeling with the wrong cause. Navigation (arrows, click, Ctrl+D)
     * keeps the glide. See docs/adr/0017. */
    void snapAnimationToTarget();

    /* All of these act on every cursor in m_cursors (a single cursor is
     * just the size-1 case) — see docs/adr/0012, decision 1, for why
     * processing highest-offset-first needs no cross-cursor bookkeeping.
     * Each loops over m_cursors calling the matching *At single-cursor
     * primitive below, passing that cursor's index (not a reference) so
     * the primitive can reach both m_cursors[i] and its index-aligned
     * m_selectionAnchors[i] — see docs/adr/0019. `extend` is only
     * meaningful for the move ops: true (Shift held) moves the head and
     * leaves the anchor fixed; false collapses an active selection to
     * its near/far edge instead of stepping from the head, matching
     * standard editor convention. */
    void insertText(const QByteArray &bytes);
    void deleteBackward();
    void deleteForward();
    void moveCursorLeft(bool extend);
    void moveCursorRight(bool extend);
    void moveCursorVertically(int lineDelta, bool extend);
    void moveCursorHome(bool extend);
    void moveCursorEnd(bool extend);
    void addCursorAtNextOccurrence();
    /* Ctrl+A — collapses to a single cursor selecting the whole
     * buffer. See docs/adr/0028. */
    void selectAll();
    void collapseToOneCursor();
    /* Sorts m_cursors and its index-aligned m_selectionAnchors together,
     * dedupes by cursor position (keeping the first anchor seen at each
     * position — same tie-break the pre-selection code implicitly had). */
    void normalizeCursors();

    /* Single-cursor primitives — operate on m_cursors[i]/m_selectionAnchors[i]. */
    void insertTextAt(int i, const QByteArray &bytes);
    void deleteBackwardAt(int i);
    void deleteForwardAt(int i);
    void moveCursorLeftAt(int i, bool extend);
    void moveCursorRightAt(int i, bool extend);
    void moveCursorVerticallyAt(int i, int lineDelta, bool extend);
    void moveCursorHomeAt(int i, bool extend);
    void moveCursorEndAt(int i, bool extend);

    /* Selection helpers — see docs/adr/0019. m_selectionAnchors[i] ==
     * m_cursors[i] means cursor i has no active selection. */
    bool hasSelectionAt(int i) const;
    size_t selectionMinAt(int i) const;
    size_t selectionMaxAt(int i) const;
    /* Deletes cursor i's active selection range as one undo entry and
     * collapses both cursor and anchor to the range's start. Must only
     * be called when hasSelectionAt(i). */
    void deleteSelectionAt(int i);

    /* Ctrl+C/X/V — see docs/adr/0020. Every selected cursor's text is
     * read straight out of m_cache (already a full buffer mirror, per
     * ADR 0006) and joined with '\n' for a multi-cursor copy. Returns
     * false (a no-op, clipboard untouched) when no cursor has an active
     * selection. */
    bool copySelection();
    void cutSelection();
    void pasteClipboard();

    /* Fills the pixel rect(s) for [start, end) across visual lines
     * [firstLine, lastLine), one rect per line — the same per-line
     * splitting/xForColumn measurement the selection highlight used
     * before this phase, now shared with the find/replace-match
     * highlight too. See docs/adr/0019, docs/adr/0021. */
    void highlightRange(QPainter &painter, size_t start, size_t end, int firstLine, int lastLine,
                         const QColor &color) const;
    /* A thin straight underline across [start, end), one per visual line
     * spanned — same per-line splitting as highlightRange, but a stroked
     * line instead of a filled rect. Was a wavy zigzag (docs/adr/0029);
     * now dim by default, brightening with a left-to-right wipe while
     * the cursor sits on that line — see docs/adr/0041. */
    void drawDiagnosticUnderline(QPainter &painter, size_t start, size_t end, int firstLine, int lastLine,
                                  const QColor &color) const;

    /* Centered logo + a few essential shortcuts, shown over an empty
     * buffer — see docs/adr/0042. Painted last (on top of everything
     * else, though there's nothing else to paint when the buffer is
     * actually empty) in absolute widget coordinates, independent of
     * scroll/gutter, at m_welcomeOverlayOpacity. */
    void drawWelcomeOverlay(QPainter &painter) const;
    /* Called every frame from updateAnimation(), unconditionally — same
     * "always live, transition smoothness is opt-in" convention
     * updateDiagnosticLineHighlights() already uses. Eases (or snaps)
     * m_welcomeOverlayOpacity toward 1 while m_cache is empty, else
     * toward 0 — so it fades in on an empty buffer (including at
     * launch) and fades back out the moment there's anything typed,
     * reappearing if it's all deleted again, regardless of whether
     * that got saved in between. */
    void updateWelcomeOverlayOpacity();

    /* Find/replace — see docs/adr/0021. Plain substring, ASCII-
     * case-insensitive (QByteArray::toLower() is ASCII-only; documented
     * v1 simplification). */
    void recomputeMatches();
    /* Moves to m_matches[index] (wrapping either direction), selecting
     * its range like any other selection — replaceCurrentMatch then
     * just reuses insertText's existing selection-replace path. */
    void jumpToMatch(int index);
    int nearestMatchAtOrAfter(size_t offset) const;

    void ensureCursorVisible();
    void save();

    /* :compile — see docs/adr/0025. Reads `build_command` from config
     * (no default; unconfigured is reported in the output panel, not
     * guessed), substitutes %f for the current file's path, spawns it
     * via a shell (so config values can use shell syntax like `&&`)
     * with the file's directory as cwd, and starts m_compilePollTimer
     * streaming its output. A no-op (reports why) if no file is open,
     * no build_command is configured, or a build is already running. */
    void compile();
    /* m_compilePollTimer's slot: drains whatever output is currently
     * available into the output panel, and stops itself once the
     * process has exited. */
    void pollCompile();
    /* Shared by Ctrl+Shift+O and `:output` — see docs/adr/0025. */
    void toggleOutputPanel();

    /* LSP diagnostics — see docs/adr/0029. Reads `lsp_command` from
     * config (no default) at construction time; a no-op (m_lspClient
     * stays null) if unconfigured, the file isn't .c/.h (same gate
     * Tree-sitter highlighting already uses), or the server fails to
     * start/handshake. */
    void startLspClientIfConfigured();
    /* m_lspPollTimer's slot. */
    void pollLsp();
    /* Called from refreshCache() — the one choke point every edit
     * already passes through — so results never go stale after the
     * first edit, the gap this phase exists to close. A no-op if no
     * LSP client is running. */
    void sendLspDidChange();
    QColor colorForSeverity(int severity) const;
    /* Worst (lowest-numbered) severity among any diagnostic spanning
     * `line`, or 0 if none — shared by the line-highlight and gutter-dot
     * passes so the O(lines * diagnostics) scan (see docs/adr/0029) only
     * has one implementation. */
    int worstSeverityForLine(int line) const;
    /* Current eased 0..1 focus value for `line`'s highlight, or 0 if it
     * has no entry in m_diagnosticLineHighlights (never focused, or
     * fully settled back to unfocused). See docs/adr/0041. */
    double diagnosticRevealForLine(int line) const;
    /* Called every frame from updateAnimation(), unconditionally (even
     * with animations off, where it just snaps reveal to target
     * instantly — same convention paintEvent's gutter current-line-
     * number brightness already uses). Sets target = 1 for whichever
     * line the primary cursor is on, if that line has a diagnostic, and
     * 0 for every other tracked line; eases (or snaps) each entry's
     * reveal toward its target; drops entries once settled at 0. */
    void updateDiagnosticLineHighlights();

    /* Automatic completion — see docs/adr/0030. Called at the end of
     * refreshCache(), the same choke point sendLspDidChange() uses, so
     * it re-evaluates after every edit. Shows/refreshes/dismisses the
     * popup based on whether the byte immediately before the single,
     * non-selecting cursor looks like something worth completing
     * (an identifier character, or a member-access trigger — '.' or
     * the '>' of "->"); any other context dismisses whatever's open
     * instead of firing a request, so the popup doesn't appear after
     * every space or newline. */
    void requestCompletionIfAppropriate();
    void applyCompletionResult(const AseJsonValue *result);
    /* Replaces [m_completionPrefixStart, cursor) with the popup's
     * selected item's insertText, reusing insertText()'s own "replace
     * the active selection" path via a temporary single-cursor
     * selection — then dismisses. Only meaningful while the popup is
     * showing (Enter/Tab call this; both are checked against
     * m_completionPopup->isShowingPopup() first). */
    void acceptCompletion();
    void dismissCompletion();
    /* Scans backward from `offset` while bytes are ASCII identifier
     * characters — the same byte-level "ASCII v1 simplification" this
     * codebase already applies to LSP character offsets (docs/adr/0029),
     * used here to find where a completion replacement should start. */
    size_t completionPrefixStart(size_t offset) const;

    /* Mouse-hover info (textDocument/hover) — see docs/adr/0030. */
    /* Called from mouseMoveEvent whenever the pointer moves with no
     * button held. Leaves an already-shown tooltip alone if the mouse
     * is still within the word range it covers; otherwise dismisses it
     * and (re)starts m_hoverTimer's pause-before-request delay. */
    void scheduleHoverRequest(const QPoint &viewportPos);
    /* m_hoverTimer's single-shot slot: fires the actual request once
     * the pointer has paused for the delay. */
    void requestHoverNow();
    void applyHoverResult(const AseJsonValue *result);
    void dismissHover();

    /* Ctrl+Z / Ctrl+Shift+Z — see docs/adr/0018. Both restore m_cursors
     * from the undo stack's recorded snapshot rather than deriving a
     * position, refreshCache(), and snapAnimationToTarget() so the edit
     * (like typing) renders instantly, not glided. */
    void undo();
    void redo();
    void applyUndoResult(size_t *cursors, size_t count);

    size_t offsetForPoint(const QPoint &pos) const;
    int lineForOffset(size_t offset) const;
    int columnForOffset(size_t offset, int line) const;
    size_t offsetForLineColumn(int line, int column) const;

    AseBuffer *m_buffer;
    QString m_filePath;
    AseUndoStack *m_undo;
    /* Set true by every mutating op (including undo/redo — a v1
     * simplification, not tracking the exact saved stack position, see
     * docs/adr/0023), cleared by a successful save(). */
    bool m_dirty = false;

    AseConfig *m_config = nullptr;
    QString m_configPath;
    QDateTime m_configModified;
    QTimer *m_configTimer;
    QColor m_backgroundColor;
    QColor m_textColor;
    QColor m_selectionColor;
    QColor m_findMatchColor;      /* current find/replace match — see docs/adr/0021 */
    QColor m_panelBackgroundColor; /* floating chrome (FindBar, ...) — see docs/adr/0022 */
    bool m_animationsEnabled = false;

    AseSyntax *m_syntax = nullptr; /* null for unsupported file types — see docs/adr/0007 */
    QVector<AseHighlightSpan> m_highlights;

    QByteArray m_cache;
    QVector<int> m_lineStarts;

    QVector<size_t> m_cursors {0}; /* always non-empty, sorted ascending, de-duplicated */
    /* Index-aligned with m_cursors, same size always — see docs/adr/0019.
     * m_selectionAnchors[i] == m_cursors[i] means cursor i has no active
     * selection; otherwise the range is [min, max) of the pair. */
    QVector<size_t> m_selectionAnchors {0};

    /* Find/replace state — see docs/adr/0021. m_findNeedle empty means
     * no active search (the bar is closed, or its field is empty).
     * Kept in the *original* case: comparison lower-cases fresh copies
     * of both needle and haystack on every recompute, but the matched
     * byte range's length always equals m_findNeedle's own length
     * (ASCII-only case folding never changes a byte's length). */
    QByteArray m_findNeedle;
    QVector<size_t> m_matches;
    int m_currentMatch = -1;
    FindBar *m_findBar = nullptr;
    FileBrowserPanel *m_fileBrowser = nullptr;
    CommandLine *m_commandLine = nullptr;
    OutputPanel *m_outputPanel = nullptr;
    HelpPanel *m_helpPanel = nullptr;
    AboutPanel *m_aboutPanel = nullptr;
    AseProcess *m_compileProcess = nullptr;
    QTimer *m_compilePollTimer;

    /* LSP — see docs/adr/0029. m_lspClient null means no LSP for this
     * buffer (unconfigured, wrong file type, or the server failed to
     * start) — every LSP-touching method already checks that first. */
    AseLspClient *m_lspClient = nullptr;
    QString m_lspUri;
    int m_lspVersion = 1; /* didOpen implicitly sends version 1; didChange starts at 2 */
    QTimer *m_lspPollTimer;
    QVector<GuiDiagnostic> m_diagnostics;
    QColor m_diagnosticErrorColor;
    QColor m_diagnosticWarningColor;
    /* At most a couple of entries alive at once in practice — one
     * wiping in on the newly-focused line, one wiping back out on the
     * line that just lost focus. See docs/adr/0041. */
    QVector<DiagnosticLineHighlight> m_diagnosticLineHighlights;

    /* Completion — see docs/adr/0030. m_completionPopup null means no
     * popup wired up (shouldn't happen once main.cpp runs, but every
     * call site checks anyway, same defensiveness as the other panel
     * pointers). No request-sequence tracking: a request is fast and
     * local, so an out-of-order response is rare and, since nothing is
     * ever auto-inserted, at worst shows a one-keystroke-stale list
     * that the very next response corrects — documented v1
     * simplification. */
    CompletionPopup *m_completionPopup = nullptr;
    size_t m_completionPrefixStart = 0;
    /* Set by acceptCompletion() right before its insertText() call,
     * which (via refreshCache()) would otherwise immediately retrigger
     * requestCompletionIfAppropriate() and reopen a popup showing the
     * very item just accepted — real editors don't reopen the list the
     * instant you've accepted from it. Consumed (and cleared) by the
     * very next requestCompletionIfAppropriate() call, so it only ever
     * suppresses that one, immediately-following request. */
    bool m_suppressNextCompletionTrigger = false;

    /* Hover — see docs/adr/0030. */
    HoverPanel *m_hoverPanel = nullptr;
    QTimer *m_hoverTimer;
    QPoint m_hoverPendingPos;     /* viewport-local pixel pos the pending/last request was for */
    size_t m_hoverPendingOffset = 0;
    /* The buffer range the *currently shown* tooltip covers — lets
     * scheduleHoverRequest tell "still hovering the same word, leave it
     * alone" from "moved to a new word, restart the delay" without
     * needing the server's own range until a response actually arrives. */
    size_t m_hoverShownRangeStart = 0;
    size_t m_hoverShownRangeEnd = 0;

    int m_scrollLine = 0;
    int m_scrollX = 0; /* leftmost visible pixel, not column — see docs/adr/0014 */
    int m_desiredColumn = -1; /* sticky column — single-cursor mode only, see docs/adr/0012 */
    QString m_lineNumberMode = QStringLiteral("absolute"); /* "off" / "absolute" / "relative" */

    /* Rendered (possibly still-easing) counterparts of m_scrollLine/X and
     * m_cursors — see docs/adr/0015. Equal to the logical values whenever
     * animations are off or nothing is moving. */
    double m_renderedScrollLine = 0.0;
    double m_renderedScrollX = 0.0;
    QVector<QPointF> m_renderedCaretPos;
    /* Eased 0..1 — see docs/adr/0042. */
    double m_welcomeOverlayOpacity = 0.0;

    QFont m_font;
    int m_lineHeight = 0;
    int m_charWidth = 0;
    /* One QFontMetrics per capture-style variant actually used, cached
     * in applyConfig() — see docs/adr/0017. */
    QFontMetrics m_metrics {m_font};
    QFontMetrics m_boldMetrics {m_font};
    QFontMetrics m_italicMetrics {m_font};

    QTimer *m_blinkTimer;
    bool m_caretVisible = true;
    /* Ticks since the last cursor-moving action. Drives both the hard
     * blink's toggle and the animated fade's phase (docs/adr/0017) — a
     * single counter, reset to 0 by resetCaretBlink(), so "stay visible
     * while active" holds for either mode without special-casing. */
    int m_idleTicks = 0;
};

#endif /* ASE_EDITOR_VIEWPORT_H */

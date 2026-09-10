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

class QTimer;
class QPainter;
class FindBar;
class FileBrowserPanel;
class CommandLine;
class OutputPanel;
class HelpPanel;
class AboutPanel;

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
    /* A wavy underline (small zigzag QPainterPath) across [start, end),
     * one per visual line spanned — same per-line splitting as
     * highlightRange, but a stroked path instead of a filled rect. Used
     * for diagnostic squiggles. See docs/adr/0029. */
    void drawSquiggle(QPainter &painter, size_t start, size_t end, int firstLine, int lastLine,
                       const QColor &color) const;

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

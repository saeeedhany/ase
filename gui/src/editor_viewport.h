#ifndef ASE_EDITOR_VIEWPORT_H
#define ASE_EDITOR_VIEWPORT_H

#include <QByteArray>
#include <QColor>
#include <QDateTime>
#include <QFont>
#include <QFontMetrics>
#include <QPointF>
#include <QHash>
#include <QPointer>
#include <QString>
#include <QVector>
#include <QWidget>

#include "notification.h"
#include "ase/vcs.h"
#include "command_registry.h"
#include "syntax_worker.h"
#include "text_edit.h"
#include "vim_pending.h"

extern "C" {
#include "ase/buffer.h"
#include "ase/config.h"
#include "ase/lsp_client.h"
#include "ase/plugin_host.h"
#include "ase/process.h"
#include "ase/syntax.h"
#include "ase/undo.h"
}

/* The core struct's `message` is only valid during the callback. */
struct GuiDiagnostic {
    int startLine;
    int startChar;
    int endLine;
    int endChar;
    int severity; /* 1=Error, 2=Warning, 3=Information, 4=Hint — per LSP */
    QString message;
};

/* `reveal` eases toward `target`, which is 1 while the cursor is on
 * `line`. Settled-at-zero entries are dropped. See docs/adr/0041. */
struct DiagnosticLineHighlight {
    int line = -1;
    double reveal = 0.0;
    double target = 0.0;
};

/* Parsed from completion JSON; converted to CompletionPopup::Item at
 * the last moment. See docs/adr/0030. */
struct GuiCompletionItem {
    QString label;
    QString insertText;
    QString detail;
};

class QThread;
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
 * Custom-painted text viewport, owning the AseBuffer it renders. The
 * panels floating over it are input widgets only; the logic lives here.
 * See docs/adr/0002, 0006, 0012, 0019, 0021, 0023, 0025.
 */

/* For the status bar's language-server segment. See docs/adr/0063. */
#include "lsp_registry.h"
#include "lsp_state.h"

class EditorViewport : public QWidget {
    Q_OBJECT

public:
    EditorViewport(AseBuffer *buffer, QString filePath, QWidget *parent = nullptr);
    ~EditorViewport() override;

    /* Plain pointers, not signals — the panels call back into this
     * class directly. */
    void setFindBar(FindBar *bar) { m_findBar = bar; }
    void setFileBrowser(FileBrowserPanel *panel) { m_fileBrowser = panel; }
    void setCommandLine(CommandLine *panel) { m_commandLine = panel; }
    void setOutputPanel(OutputPanel *panel) { m_outputPanel = panel; }
    void setHelpPanel(HelpPanel *panel) { m_helpPanel = panel; }
    /* One per window, like the output panel — see docs/adr/0096. */
    void setLspRegistry(LspRegistry *registry) { m_lspRegistry = registry; }
    /* Dropped when another buffer claims the same uppercase letter. */
    void clearLocalMark(char name) { m_vimMarks.remove(name); }
    /* Called by the registry when this buffer's shared server changes
     * state, including when it joins one that is already up. */
    void onLspStateChanged(LspState state, const QString &serverName);
    void setCompletionPopup(CompletionPopup *popup) { m_completionPopup = popup; }
    void setHoverPanel(HoverPanel *panel) { m_hoverPanel = panel; }

    /* :w/:q/:compile/:output; an unknown command is a silent no-op.
     * See docs/adr/0025. */
    void runCommand(const QString &command);
    /* :name dispatch into the plugin registry, checked after every
     * built-in so plugins can't shadow one. False if unregistered. */
    bool runPluginCommand(const QString &name);
    /* The plugin ABI reaches the editor through these — see
     * docs/adr/0141. Public because the C vtable's functions are free
     * functions, not members. */
    AseBuffer *buffer() const { return m_buffer; }
    bool primarySelection(size_t *start, size_t *end) const;
    void requestPluginCursor(size_t offset);
    void requestPluginSelection(size_t start, size_t end);

    QString filePath() const { return m_filePath; }

    /* Enough to put the caret and the viewport back where the last
     * session left them — see docs/adr/0111. */
    size_t cursorOffset() const { return m_cursors.isEmpty() ? 0 : m_cursors[0]; }
    /* How many carets there are, and where each one is — for the status
     * bar's multi-cursor count and for asserting on it. */
    int cursorCount() const { return static_cast<int>(m_cursors.size()); }
    /*
     * What a screen reader is shown — see docs/adr/0146.
     *
     * Offsets here are QString indices, which is what the accessibility
     * layer speaks; everything else in this class counts UTF-8 bytes.
     * The two agree only while a file is ASCII, so the conversion lives
     * here rather than at each call site.
     */
    QString a11yText() const;
    int a11yCharacterCount() const;
    int a11yCursorPosition() const;
    void setA11yCursorPosition(int index);
    bool a11ySelection(int *start, int *end) const;
    void setA11ySelection(int start, int end);
    QRect a11yCharacterRect(int index) const;
    int a11yOffsetAtPoint(const QPoint &point) const;
    /* Line `index` falls on, and where that line starts and ends. */
    void a11yLineAt(int index, int *start, int *end) const;
    /*
     * Whether edits and caret moves are announced.
     *
     * Normally this is QAccessible::isActive(), which asks the platform
     * accessibility plugin — and answers false forever when there is
     * none, which is the case on a plain CI runner. Forcing it on is
     * how the notification path gets exercised anywhere at all. See
     * docs/adr/0146.
     */
    /* The smallest edit explaining the difference between two texts —
     * what a reader is told changed. Pure, and public, because Qt
     * delivers the event itself differently across versions and this is
     * the part that can be wrong. See docs/adr/0146. */
    static void a11yEditBetween(const QString &before, const QString &after, int *position,
                                 QString *removed, QString *inserted);
    static bool announcingToAccessibility();
    static void setAccessibilityAlwaysOn(bool alwaysOn);

    /* The shape a selection paints as — public so it can be asserted on.
     * See docs/adr/0144. */
    QVector<QRectF> highlightRects(size_t start, size_t end, int firstLine, int lastLine,
                                    bool linewise) const;
    int charWidth() const { return m_charWidth; }
    QVector<size_t> cursorOffsets() const { return m_cursors; }
    int scrollLine() const { return m_scrollLine; }
    void restorePosition(size_t cursor, int scrollLine);
    bool isDirty() const;
    void requestOpenFile(const QString &path) { emit fileOpenRequested(path); }
    /* Synchronous; see docs/adr/0066 for the caps that bound it. */
    void searchProject(const QString &needle);
    /* Same search, previewed as what it would change. See
     * docs/adr/0131. */
    void replaceInProject(const QString &needle, const QByteArray &replacement);

    /* The identifier the caret is in, for prefilling a rename prompt
     * with the name being changed. Empty when the caret is not in one. */
    QString symbolUnderCursor() const;
    /* Asks the server what renaming it would change, and previews the
     * answer. Nothing is edited here. See docs/adr/0132. */
    void renameSymbolTo(const QString &newName);
    void applyLspRename(const AseJsonValue *result, const char *error_message);
    void goToDefinition();
    /* LSP's other half — see docs/adr/0116. */
    void findReferences();
    void showDocumentSymbols();
    void applyLspReferences(const AseJsonValue *result, const char *error_message);
    void applyLspDocumentSymbols(const AseJsonValue *result, const char *error_message);
    void goToLine(int oneBasedLine);
    /* Restores an exact column rather than the first non-blank. */
    void goToLineColumn(int oneBasedLine, int oneBasedColumn);

    /* Applies all of them as a single undo group, so one `u` takes back
     * this file's whole share of a multi-file edit. Returns how many
     * landed; one naming a place that is not there is skipped rather
     * than fatal, because a hit can outlive the file it was found in. */
    int applyLineEdits(const QVector<TextEdit> &edits);
    /* 1-based. */
    int cursorLine() const { return lineForOffset(m_cursors.isEmpty() ? 0 : m_cursors[0]) + 1; }
    int cursorColumn() const;
    /* "About to move somewhere h/j/k/l wouldn't reach" — the window
     * snapshots the position. See docs/adr/0070. */
    void recordJump() { emit jumpRecorded(); }
    /* The one way to say anything to the user. */
    void notify(NotifyLevel level, const QString &text) { emit messagePosted(level, text); }
    LspState lspState() const { return m_lspState; }
    QString lspServerName() const { return m_lspServerName; }
    /* Starts the language server on first activation, not in the
     * constructor — ten open files shouldn't spawn ten servers. */
    void onActivated();
    /* Only MainWindow can know this: a pathless Ctrl+N buffer opened
     * mid-session must not be greeted. See docs/adr/0058. */
    void armWelcomeGreeting() { m_welcomeEligible = true; }

    /* The commands this buffer owns, as opposed to the window's. Read
     * by binding validation and by --dump-docs. */
    const CommandRegistry &ownCommands() const { return m_ownCommands; }

    /* What a crash snapshot is filed under: the path once there is one,
     * and a per-process name before that. See docs/adr/0127. */
    QString recoveryKey() const;
    /* Where snapshots live, so the window can walk them at startup. */
    QString recoveryDir() const { return m_recoveryDir; }
    /* True while this buffer has never been saved anywhere. */
    bool isUntitled() const { return m_filePath.isEmpty(); }
    /* Adopts a snapshot listed at startup, so a recovered untitled
     * buffer keeps writing to the same one. */
    void adoptRecoveryKey(const QString &key) { m_untitledKey = key; }
    /* Content restored from a snapshot is unsaved by definition, and
     * the undo stack cannot say so: the text went in before there was
     * one. */
    void markUnsaved() { m_historyDiscardedWhileDirty = true; }
    void saveAs(const QString &path);

    /* Panel chrome has no AseConfig of its own and reaches theme
     * through these. See docs/adr/0022. */
    QColor backgroundColor() const { return m_backgroundColor; }
    QColor textColor() const { return m_textColor; }
    QColor selectionColor() const { return m_selectionColor; }
    QColor panelBackgroundColor() const { return m_panelBackgroundColor; }
    /* Derived, so a panel border needs no config key of its own. */
    QColor panelBorderColor() const {
        QColor c = m_textColor;
        c.setAlpha(60);
        return c;
    }
    /* Derived: distinct control, no new hue. */
    QColor panelFieldColor() const {
        QColor c = m_backgroundColor;
        c.setAlpha(255);
        return c.lighter(130);
    }
    bool animationsEnabled() const { return m_animationsEnabled; }
    /* Reused for error messages, so no new hue. */
    QColor diagnosticErrorColor() const { return m_diagnosticErrorColor; }
    /* So chrome can match the text's weight and follow Ctrl+=/Ctrl+-. */
    QFont editorFont() const { return m_font; }

    /* Called by FindBar; see docs/adr/0021. */
    QString primarySelectionText() const;
    void setFindQuery(const QString &needle);
    void findNext();
    void findPrevious();
    /* `/` and `?`. Records a jump, then moves to the first match past
     * the cursor in that direction. */
    void startSearch(const QString &needle, bool forward);
    /* `n` / `N`, with the direction already resolved against the one
     * the search was made in. */
    void searchRepeat(bool forward);
    /* `*` and `#` — search for the word at or after the cursor, whole
     * words only, and leave it as the search `n` repeats. */
    void vimSearchWordUnderCursor(bool forward);
    bool searchWasForward() const { return m_searchForward; }
    /* Vim's :nohlsearch — drops the matches, keeps the needle. */
    void clearFindHighlights();
    void replaceCurrentMatch(const QByteArray &replacement);
    void replaceAllMatches(const QByteArray &replacement);

    /* These three are public only for the C callback trampolines in
     * editor_viewport.cpp; nothing else should call them. */
    void applyLspDiagnostics(const char *uri, const AseLspDiagnostic *diagnostics, size_t count);
    void applyLspCompletion(const AseJsonValue *result, const char *error_message);
    void applyLspHover(const AseJsonValue *result, const char *error_message);
    void applyLspDefinition(const AseJsonValue *result, const char *error_message);

    /* One call site, right after construction. */
    void emitInitialStatus() { ensureCursorVisible(); }

    /* True when this file is past syntax_max_kb and is deliberately
     * being shown without colours. */
    bool syntaxSkippedForSize() const { return m_syntaxOverSizeCap; }
    /* True while a worker is parsing and the window it will fill is
     * still empty, so the text is drawn plain. See docs/adr/0135. */
    bool highlightPending() const { return m_highlightDeferred; }
    /* What the capture window covers, for asserting that a worker's
     * answer actually landed. */
    int highlightWindowBytes() const { return m_captureWindowEnd - m_captureWindowStart; }

    /* Unsaved work from a session that did not end cleanly — see
     * docs/adr/0110. */
    bool hasRecoverySnapshot() const;
    bool restoreFromRecovery();
    void discardRecovery();

    /* The shared command table; the window owns it so built-ins,
     * window actions and plugin commands are all one registry. See
     * docs/adr/0113. */
    void registerCommands(CommandRegistry *registry);
    /* For the window's prefix dispatcher, which resolves a sequence and
     * then has to run it against whichever buffer is in front. */
    bool runNamedCommand(const QString &name);
    /* This buffer's commands, then the window's, then any plugin's.
     * One order, shared by key bindings and the `:` line. */
    bool runCommandByName(const QString &name);

    /* For the help panel, which has to show the bindings the user
     * actually has rather than the defaults. */
    const AseConfig *config() const { return m_config; }

signals:
    /* Vim's showcmd — the half-typed command, for the status bar. */
    void pendingInputChanged(const QString &keys);

    /* So the window can tell the seam which side is live. */
    void focusChanged(bool focused);

    void fileOpenRequested(const QString &path);
    /* 1-based line. The window owns the buffer list, this owns the
     * cursor, and a definition in another file needs both. */
    void fileOpenAtLineRequested(const QString &path, int line);
    /* Carries nothing: the window knows which viewport is active. */
    void jumpRecorded();
    /* From ensureCursorVisible(), so every cursor move and edit gets it
     * for free. 1-based; modeLabel is empty when Vim mode is off. */
    void statusChanged(int line, int column, bool dirty, const QString &modeLabel);
    /* Everything the editor says, on one signal. See docs/adr/0062. */
    void messagePosted(NotifyLevel level, const QString &text);
    /* Only on an actual change. */
    void lspStateChanged(LspState state, const QString &serverName);
    /* Uppercase marks name a file as well as a position, which only the
     * window knows about — see docs/adr/0098. */
    void globalMarkSetRequested(char name);
    void globalMarkJumpRequested(char name, bool exact);
    /* :q / :q!. The window owns the buffer list, so it decides what
     * closing the last one means. force skips the unsaved-changes
     * prompt. */
    void closeRequested(bool force);

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;

private:
    void loadConfig();
    void applyConfig();
    void checkConfigReload();
    /* Defaults, then the user file, then the nearest .ase.conf above
     * this buffer's own file. */
    void refreshAnimationClock();
    void rebuildConfig();
    /* Tells the panels, the status bar and the buffer bar that the
     * colours changed. See docs/adr/0115. */
    void repaintForNewTheme();
    void rebuildSyntax();

    /* Gutter marks from `git diff` — see docs/adr/0112. */
    void refreshVcsMarks();
    void pollVcs();
    AseVcsLineStatus vcsStatusForLine(int line) const;
    QColor colorForVcsStatus(AseVcsLineStatus status) const;
    static constexpr double kVcsBarWidth = 2.0;
    static constexpr int kVcsOutputCap = 4 * 1024 * 1024;
    /* Past this the parse moves to a worker thread rather than running
     * on the keystroke. It used to wait for a pause in typing instead,
     * which meant a large file was plain while you typed — see
     * docs/adr/0107 and docs/adr/0135. */
    static constexpr int kSyncHighlightBytes = 256 * 1024;
    /* Present but plainly not taking input. Bright enough to find, dim
     * enough that it is not where your eye goes. */
    static constexpr int kUnfocusedCaretAlpha = 70;
    /* Long enough that a burst of typing writes one snapshot, short
     * enough that little is at risk. */
    static constexpr int kRecoveryDelayMs = 900;
    void armRecoverySnapshot();
    void writeRecoverySnapshot();
    void openConfigFile();
    void runTheme(const QString &argument);
    /* Re-reads <config dir>/themes/. Cheap, and called wherever the
     * answer has to be current — a file dropped in does not touch
     * config.ase, so nothing else would notice it. See docs/adr/0133. */
    void reloadThemeFiles();
    bool writeConfigSetting(const QString &key, const QString &value);
    /* Rebuilds the font and its cached metrics; touches no config. */
    void rebuildFont(int pointSize);
    bool configFlag(const char *key, bool fallback) const;
    int configAlpha(const char *key, int fallbackPercent) const;
    /* Ctrl+=/Ctrl+-. A hot-reload doesn't clear an active override;
     * Ctrl+0 is the only way back to the configured size. */
    void adjustFontSize(int delta);
    void resetFontSize();
    void refreshCache();
    /* Re-runs the syntax query over a padded window only when the one
     * it holds doesn't cover [startByte, endByte). */
    void ensureCaptureWindow(int startByte, int endByte, bool force);
    /* Byte range of the lines on screen, from m_renderedScrollLine — the
     * frame about to be drawn, not the settled target. */
    void visibleByteRange(int *startByte, int *endByte) const;
    /* Brings m_captureAt up to date for what is on screen. Call after
     * any change to m_renderedScrollLine, before reading captures. */
    void ensureCaptureWindowForViewport();
    void drawLine(QPainter &painter, int start, int end, int y);
    QFont fontForCapture(AseHighlightCapture capture) const;
    QColor colorForCapture(AseHighlightCapture capture) const;
    /* Cached per applyConfig(): QFontMetrics construction isn't free
     * and this was running per styled run per frame. */
    const QFontMetrics &metricsForCapture(AseHighlightCapture capture) const;
    QVector<AseHighlightCapture> capturesForLine(int start, int end) const;
    /* Measured per-run with each run's real font, not assumed via
     * column * m_charWidth. See docs/adr/0013. */
    int xForColumn(int lineStart, int lineEnd, int column) const;
    /* Inverse, measured the same way. See docs/adr/0039. */
    int columnForX(int lineStart, int lineEnd, int localX) const;

    /* 0 when line numbers are off; otherwise measured, not assumed. */
    int gutterWidth() const;
    QString gutterLabelForLine(int line, int cursorLine) const;

    /* One easing step. Called from paintEvent, not the timer, so it is
     * never stale relative to what is about to be drawn. */
    void updateAnimation();
    /* Against the current, possibly still-easing scroll position. */
    QPointF caretTargetFor(size_t cursor) const;
    /* Call on every cursor-moving action so the caret never fades
     * mid-use. See docs/adr/0016, docs/adr/0017. */
    void resetCaretBlink();
    /* Bypasses the easing for one update. Used after an edit, even with
     * animations on: a caret gliding behind fast typing reads as the
     * editor being slow. Navigation keeps the glide. */
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
    void insertTextAt(int i, const QByteArray &bytes, bool animate);
    void deleteBackwardAt(int i);
    void deleteForwardAt(int i);
    void moveCursorLeftAt(int i, bool extend);
    void moveCursorRightAt(int i, bool extend);
    void moveCursorVerticallyAt(int i, int lineDelta, bool extend);
    void ensureDesiredColumns();
    AseEditorContext *pluginContext();
    void applyPluginCursorRequest();
    /* Told to a screen reader as it happens — nothing is computed when
     * none is listening. See docs/adr/0146. */
    void notifyAccessibleTextChange(const QByteArray &before);
    void notifyAccessibleCursor();
    int m_lastAnnouncedCursor = -1;
    void schedulePluginEvents();
    void flushPluginEvents();
    void emitPluginEvent(AseEventKind event);
    bool recordExternalEdit(const QByteArray &before);
    size_t vimClampOffLineEnd(size_t offset, int line) const;
    void moveCursorHomeAt(int i, bool extend);
    void moveCursorEndAt(int i, bool extend);

    /* Selection helpers — see docs/adr/0019. m_selectionAnchors[i] ==
     * m_cursors[i] means cursor i has no active selection. */
    /* Where a Visual-mode selection actually ends. Vim's visual is
     * inclusive of the character under the cursor; this editor's own
     * selection (shift+arrows, mouse, Ctrl+D) is exclusive, and stays
     * that way — the adjustment belongs here, not in selectionMaxAt().
     * See docs/adr/0079. */
    size_t vimVisualEnd(int i) const;
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

    /* Single-cursor: any Vim key collapses to one cursor first. Returns
     * true if the key was claimed (an unrecognised key in Normal/Visual
     * is swallowed), false to let the caller's own chain handle it.
     * See docs/adr/0046. */
    enum class VimMode { Insert, Normal, Visual };
    bool vimModeActive() const { return m_vimModeEnabled; }
    /* Claimed-key handlers keyPressEvent tries in order. True means the
     * key was consumed and nothing further should see it. */
    bool handleCompletionPopupKey(QKeyEvent *event);
    bool handleBoundChord(QKeyEvent *event);
    void reportKeybindingProblems();
    QString currentModeName() const;
    bool handleVimNormalOrVisualKey(QKeyEvent *event);
    /* The pieces handleVimNormalOrVisualKey() dispatches to, in order.
     * Each runs its own reset/ensureCursorVisible/update tail, because
     * which of those a key needs differs between them. */
    bool vimResolvePendingKey(QChar qc, int key);
    bool vimAccumulateCount(QChar qc);
    bool vimApplyMotionKey(char c, int count);
    void vimApplyVisualKey(char c);
    void vimApplyNormalKey(char c, int count);
    /* '\n' counts as Blank — see vimClassifyAt in the .cpp. */
    enum class VimCharClass { Blank, Word, Punct };
    VimCharClass vimClassifyAt(size_t pos) const;
    /* A WORD is only blank versus non-blank, so `big` folds Punct into
     * Word and the same three motions serve w/e/b and W/E/B. */
    VimCharClass vimClassifyAt(size_t pos, bool big) const;
    size_t vimNextCharBoundary(size_t pos) const;
    size_t vimPrevCharBoundary(size_t pos) const;
    size_t vimWordForward(size_t pos, bool big = false) const;
    size_t vimWordEnd(size_t pos, bool big = false) const;
    /* End of the character-class run `pos` sits in. Unlike vimWordEnd,
     * which is `e` and steps on to the next word when already at a run's
     * end, this stops where the run does — what `cw` needs. */
    size_t vimWordRunEnd(size_t pos, bool big = false) const;
    size_t vimWordBackward(size_t pos, bool big = false) const;
    /* First non-blank on `line`, or its end if entirely blank. */
    /* A buffer ending in a newline gets a final m_lineStarts entry at
     * EOF, which is a position but not a line vim would count. */
    int vimLastLine() const;
    /* vim's cursor_down: `count - 1` lines below `fromLine`, clamped to
     * the last line. False when there was nowhere to go at all, which
     * fails the whole command rather than doing part of it — see
     * docs/adr/0145. */
    bool vimLineBelow(int fromLine, int count, int *target) const;
    size_t vimFirstNonBlank(int line) const;
    /* A linewise range at the buffer's end has no trailing '\n' to
     * remove, so take the preceding one instead — that deletes the line
     * rather than emptying it. See docs/adr/0060. */
    void vimReplaceRange(size_t start, size_t end, const QByteArray &text);
    void vimToggleCase(int count);
    void vimToggleCaseInPlace(QByteArray &text);
    void vimInsertPaste(size_t insertAt, const QByteArray &bytes, bool linewise);
    void vimIndentLines(int startLine, int lineCount, bool right);

    /*
     * A new line starts where the one it came from started.
     *
     * The leading whitespace is copied verbatim rather than re-rendered
     * as tabs or spaces: this editor has no tab policy to normalise
     * towards, and converting a file's existing indentation because you
     * pressed Enter is worse than inheriting whatever it already uses.
     * Vim with `expandtab` does normalise, so this is a deliberate
     * divergence — see docs/adr/0136.
     */
    QByteArray indentOfLine(int line) const;
    /* Inserts a line break plus that indent at the cursor. */
    void insertNewlineWithIndent();
    /* Vim removes an indent you never typed on top of. Called when
     * Insert mode ends. */
    void dropUnusedAutoIndent();
    void noteAutoIndentedLine(int line);

    bool m_autoIndent = true;
    /*
     * The span of lines this Insert session auto-indented, so leaving it
     * can take back the ones nothing was typed on.
     *
     * A span rather than a single line because one session can indent
     * several: `o<CR><CR><Esc>` leaves three blank lines and vim strips
     * all three, and a count multiplies them further. -1 means nothing
     * to take back. See docs/adr/0137.
     */
    int m_autoIndentFirst = -1;
    int m_autoIndentLast = -1;

    /*
     * A count given to i/a/I/A/o/O repeats the whole insert that many
     * times when it ends — `3iab` types "ababab". For o and O each
     * repeat opens its own line first. See docs/adr/0137.
     */
    int m_insertCount = 1;
    bool m_insertOpensLine = false;
    /* Set while those repeats are being replayed, so they are neither
     * re-captured for `.` nor counted again. */
    bool m_insertRepeating = false;
    void vimRepeatInsertForCount(const QByteArray &typed);
    void vimAddToNumber(int delta);
    size_t vimLinewiseDeleteStart(size_t start, size_t end) const;
    /* Never crosses a line. A miss returns the cursor unchanged.
     * See docs/adr/0069. */
    size_t vimFindInLine(char command, char target, int count) const;
    void vimApplyFindInLine(char command, char target, int count);
    /* `r` — replace `count` characters under the cursor with `target`,
     * staying in Normal mode. A no-op unless the line has that many
     * characters left, which is vim's own rule. */
    void vimReplaceChar(QChar target, int count, bool newline);
    /*
     * `R` — Replace mode. A flag on Insert rather than a fourth VimMode,
     * the same call ADR 0056 made for linewise Visual: everything that
     * matters already keys off "are we inserting", and only the typing
     * and Backspace paths differ.
     */
    /* Shorthand operators: s = c<count>l, S = cc, C = c$, D = d$,
     * X = the mirror of x. */
    void vimChangeOrInsert(size_t start, size_t end);
    size_t vimLineEndOffset(int line) const;
    /* `%` — the bracket matching the one at or after `pos` on its line,
     * or `pos` when there is none or it is unmatched. */
    size_t vimMatchBracket(size_t pos) const;

    /* A text object resolves to a byte range; `valid` is false when
     * there is nothing of that shape at or after the cursor. */
    struct VimObjectRange {
        size_t start = 0;
        size_t end = 0;
        bool valid = false;
    };
    /* `kind` is 'i' (inner) or 'a' (around); `object` names the shape —
     * w W ( ) b { } B [ ] < > " ' ` — see docs/adr/0100. */
    VimObjectRange vimTextObjectRange(char kind, char object) const;
    /* The pair enclosing `pos`, or the next one starting after it on
     * this line, which is what vim does when the cursor is outside. */
    bool vimEnclosingPair(size_t pos, char open, char close, size_t *outOpen,
                           size_t *outClose) const;
    bool vimQuotedRange(size_t pos, char quote, size_t *outOpen, size_t *outClose) const;
    void vimApplyTextObject(char kind, char object);
    /* `J` joins with a space and drops the next line's indent; `gJ`
     * takes both lines verbatim. See docs/adr/0101. */
    void vimJoinLines(int count, bool withSpace);
    /* `:[range]s/pat/rep/[flags]`. False when `command` is not a
     * substitute at all, so the caller can keep looking. */
    bool runSubstitute(const QString &command);
    void vimEnterReplaceMode(int count);
    /* One typed character, overwriting what is under the cursor, or
     * appending when the line has run out. */
    void vimReplaceTyped(const QByteArray &bytes);
    /* Backspace in Replace mode puts back what was overwritten rather
     * than deleting, which is why the originals are kept. */
    bool vimReplaceBackspace();
    void vimLeaveReplaceMode();
    /* Visual-mode `r` — every character in the selection becomes
     * `target`, except line breaks, which are left alone. */
    void vimReplaceSelection(QChar target);
    size_t vimParagraphForward(size_t pos) const;
    size_t vimParagraphBackward(size_t pos) const;
    bool vimLineIsEmpty(int line) const;
    /* Scrolls by the same amount, so the cursor keeps its row on
     * screen. `direction` is +1 or -1. */
    void vimHalfPageMotion(int direction);
    /* 0-based and clamped. Applies a pending operator linewise instead
     * of moving, if one is pending. */
    void vimGotoLine(int line);
    /* After every resolved command, so state never leaks into the next
     * keystroke. */
    void resetVimPendingState();
    /*
     * `.` — repeating the last change.
     *
     * The change is stored as the Normal-mode keys that made it plus any
     * text typed in the insert session that followed, and replayed back
     * through this same dispatcher. Keys rather than a description of
     * the edit, because that is the only form that composes: `c` + any
     * motion + any typed text needs no case of its own.
     */
    /* Every key while recording, before dispatch — so a macro replays
     * what was typed rather than what it was interpreted as. */
    void vimRecordMacroKey(QKeyEvent *event);
    void vimStopRecordingMacro();
    void vimPlayMacro(char name, int count);
    void vimSetMark(char name);
    /* `exact` is the backtick form (line and column); false is the quote
     * form, which lands on the line's first non-blank. */
    void vimJumpToMark(char name, bool exact);
    /* `d'a` and ``d`a``: the mark is the motion's target rather than a
     * place to go. Linewise for `'`, charwise-exclusive for `` ` ``. */
    void vimApplyOperatorToMark(char name, bool exact);
    void vimRecordKey(QChar qc);
    /* Called where a command actually changes the buffer, which is what
     * makes it repeatable. Yanks and motions do not call it. */
    void vimMarkChange();
    void vimBeginInsert(int count, bool opensLine);
    void vimBeginInsertCapture();
    void vimEndInsertCapture();
    void vimRepeatChange(int count);
    void vimNormalizeLinewiseSelection();
    /* Paired with vimNormalizeLinewiseSelection(). No-op unless
     * linewise. */
    void vimPrepareLinewiseMotion();
    /* With an operator pending this computes a range and applies it
     * rather than moving the cursor. */
    void vimExecuteMotion(char m, int count);
    /* Each opens exactly one undo group around direct buffer calls,
     * never through insertText()/deleteBackward() — groups can't nest.
     *
     * `linewise` says how the text is *stored* in the register, not how
     * it is removed: a linewise delete at the buffer's end takes the
     * newline above, which must not reach the register or `p` pastes a
     * blank line. See docs/adr/0061. */
    void vimDeleteRange(size_t start, size_t end, bool linewise = false);
    void vimSetRegister(const QByteArray &text, bool linewise);
    /* The register this command names, consumed once. */
    char vimTakeRegister();
    void vimYankRange(size_t start, size_t end, bool linewise);
    void vimChangeRange(size_t start, size_t end, bool linewise = false);
    void vimDeleteLines(int startLine, int count);
    void vimYankLines(int startLine, int count);
    void vimPasteAfter();
    void vimPasteBefore();
    /* The one place a resolved operator+motion commits. */
    void vimApplyPendingOperatorCharwise(size_t start, size_t end);
    void vimApplyPendingOperatorLinewise(int startLine, int lineCount);
    /* Raw buffer calls, not moveCursorHomeAt + insertText("\n"): that
     * lands the cursor a line too low. */
    void vimOpenLineAbove();
    /* Empty string (and *width = m_charWidth) where there is no real
     * character to cover — end of line or buffer. See docs/adr/0047. */
    QString vimBlockGlyphAt(size_t cursor, int *width, AseHighlightCapture *capture) const;

    /* One rect per visual line. Shared by selection and match
     * highlighting. */
    /* `linewise` paints whole lines rather than the characters between
     * two offsets — see docs/adr/0144. */
    void highlightRange(QPainter &painter, size_t start, size_t end, int firstLine, int lastLine,
                         const QColor &color, bool linewise = false) const;
    /* Dim by default, brightening with a left-to-right wipe while the
     * cursor is on that line. See docs/adr/0041. */
    void drawDiagnosticUnderline(QPainter &painter, size_t start, size_t end, int firstLine, int lastLine,
                                  const QColor &color) const;

    /* Absolute widget coordinates, independent of scroll/gutter.
     * See docs/adr/0042. */
    void drawWelcomeOverlay(QPainter &painter) const;
    /* Eases toward 1 while m_cache is empty, else toward 0. */
    void updateWelcomeOverlayOpacity();

    /* Plain substring, ASCII-case-insensitive. See docs/adr/0021. */
    void recomputeMatches();
    int lastMatchBefore(size_t offset) const;
    void searchStep(bool forward);
    void notifyNoMatches();
    /* Wraps either direction, selecting the range like any other. */
    /* `select` leaves the match selected, which is what the find bar's
     * replace acts on. Vim's search does not select: it puts the cursor
     * on the first character of the match and stops. */
    void jumpToMatch(int index, bool select = true);
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
    void compile(const QString &command);
    void runBuild(const QString &command, const QString &directory);
    /* Drains available output; stops itself once the process exits. */
    void pollCompile();
    /* Shared by Ctrl+Shift+O and `:output` — see docs/adr/0025. */
    void toggleOutputPanel();
    void closeOutputPanel();

    /* No-op if lsp_command is unset, the file isn't .c/.h, or the
     * server fails to start. See docs/adr/0029. */
    void startLspClientIfConfigured();
    /* Emits only when the state actually changed. */
    void setLspState(LspState state);
    /* A server that dies after a good start otherwise leaves the editor
     * reporting one that isn't there. */
    void releaseLspClient();
    /* From refreshCache(), the choke point every edit passes through. */
    void sendLspDidChange();
    QColor colorForSeverity(int severity) const;
    /* Lowest-numbered severity spanning `line`, or 0. */
    int worstSeverityForLine(int line) const;
    double diagnosticRevealForLine(int line) const;
    /* Targets 1 for the cursor's line, 0 for every other tracked line,
     * then eases and drops entries settled at 0. */
    void updateDiagnosticLineHighlights();

    /* Fires only when the byte before the cursor is an identifier
     * character or a member-access trigger; anything else dismisses,
     * so the popup doesn't appear after every space. See docs/adr/0030. */
    void requestCompletionIfAppropriate();
    void applyCompletionResult(const AseJsonValue *result);
    /* Only meaningful while the popup is showing. */
    void acceptCompletion();
    void dismissCompletion();
    /* Scans back over ASCII identifier bytes. */
    size_t completionPrefixStart(size_t offset) const;

    /* Leaves a shown tooltip alone while the pointer stays within the
     * word it covers; otherwise dismisses and restarts the delay. */
    void scheduleHoverRequest(const QPoint &viewportPos);
    void requestHoverNow();
    void applyHoverResult(const AseJsonValue *result);
    void dismissHover();

    /* Guards the input handlers so the document is inert while a modal
     * panel is up. The output panel is deliberately not one. */
    bool isModalPanelOpen() const;

    /* Restore cursors from the stack's snapshot rather than deriving
     * them, and render instantly. See docs/adr/0018. */
    /*
     * One undo step. While an insert session is open these do nothing —
     * the session is the group — so a whole `i...<Esc>` undoes at once
     * the way vim does, instead of a step per keystroke. The core stack
     * does not nest groups (core/include/ase/undo.h), which is why this
     * is a flag here rather than a depth count there.
     */
    void beginUndoStep();
    void endUndoStep();
    void beginUndoSession();
    void endUndoSession();

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
    /* Undo state id at the last save; 0 means "as loaded". isDirty()
     * compares against the current id, so undoing an edit makes the
     * file clean again. See docs/adr/0059. */
    size_t m_savedStateId = 0;
    /* The case an id can't describe: a plugin mutated the buffer and
     * the history was discarded, so a fresh stack reads "as loaded". */
    bool m_historyDiscardedWhileDirty = false;

    /* The window's: buffer list, jumplist, panel size. */
    CommandRegistry *m_commands = nullptr;
    /* This buffer's own, so a command always acts on the buffer whose
     * keystroke it was. See docs/adr/0119. */
    CommandRegistry m_ownCommands;
    QTimer *m_recoveryTimer = nullptr;
    QTimer *m_vcsPollTimer = nullptr;
    AseProcess *m_vcsProcess = nullptr;
    AseVcsDiff *m_vcsDiff = nullptr;
    QByteArray m_vcsOutput;
    int m_vcsLineCount = -1;
    QString m_recoveryDir;
    /* Set by `:theme`; empty means whatever the config says. */
    QString m_sessionTheme;
    /* What findReferences() asked about, for the summary line. */
    QString m_lspReferenceSymbol;
    /* What the rename in flight is changing, and to what. Kept because
     * the reply carries neither. */
    QString m_lspRenameFrom;
    QString m_lspRenameTo;
    /* Vim's showcmd: keys typed toward a command not yet resolved. */
    QString m_vimPendingKeys;
    bool m_syntaxOverSizeCap = false;
    AseConfig *m_config = nullptr;
    QString m_configPath;
    QDateTime m_configModified;
    QString m_projectConfigPath;
    QDateTime m_projectConfigModified;
    QTimer *m_configTimer = nullptr;
    QColor m_backgroundColor;
    QColor m_textColor;
    QColor m_selectionColor;
    QColor m_findMatchColor;      /* current find/replace match — see docs/adr/0021 */
    QColor m_panelBackgroundColor; /* floating chrome (FindBar, ...) — see docs/adr/0022 */
    bool m_animationsEnabled = false;

    AseSyntax *m_syntax = nullptr; /* null for unsupported file types — see docs/adr/0007 */

    /*
     * The parse for a file too big to do on the keystroke. Exactly one
     * of m_syntax and m_syntaxThread is ever live: below the threshold
     * the parse is immediate and on this thread, above it there is a
     * worker and this thread never touches a parser at all. See
     * docs/adr/0135.
     */
    QThread *m_syntaxThread = nullptr;
    SyntaxWorker *m_syntaxWorker = nullptr;
    /* Bumped by every refreshCache(). A result carrying an older one
     * describes a buffer that has since changed and is dropped. */
    quint64 m_syntaxVersion = 0;
    /* One request in flight at a time: typing faster than the parser
     * would otherwise queue a parse per keystroke, each already stale
     * when it started. The latest wanted window is kept instead and
     * asked for when the worker comes back. */
    bool m_syntaxBusy = false;
    bool m_syntaxWantsAnother = false;
    int m_syntaxPendingStart = 0;
    int m_syntaxPendingEnd = 0;

    void startSyntaxWorker(AseLanguage language);
    void stopSyntaxWorker();
    /* Asks the worker, or remembers to, once it is free. */
    void requestAsyncHighlight(int windowStart, int windowEnd);
    void applyWorkerSpans(const QVector<AseHighlightSpan> &spans, quint64 version, int windowStart,
                           int windowEnd);
    /* Loaded from <config dir>/plugins/; reached via `:name`. */
    AsePluginHost *m_pluginHost = nullptr;
    /* What a plugin command sees of this editor, and what it asked for
     * while it ran — see docs/adr/0141. */
    AseEditorContext *m_pluginContext = nullptr;
    /* Coalescing for the two hot events — see docs/adr/0142. The timer
     * only exists while something is listening. */
    QTimer *m_pluginEventTimer = nullptr;
    bool m_bufferChangedSinceEmit = false;
    size_t m_lastEmittedCursor = 0;
    long long m_pluginCursorRequest = -1;
    long long m_pluginSelectionStart = -1;
    long long m_pluginSelectionEnd = -1;

    QVector<AseHighlightSpan> m_highlights;
    /* m_highlights flattened to one byte per buffer byte — the form
     * every render lookup wants. See docs/adr/0053. */
    QVector<uint8_t> m_captureAt;
    /* The range m_captureAt is valid for. See docs/adr/0072. */
    int m_captureWindowStart = 0;
    int m_captureWindowEnd = 0;
    /* Set while a large file is waiting for the debounced parse. See
     * docs/adr/0124. */
    bool m_highlightDeferred = false;

    QByteArray m_cache;
    /* Stable for this buffer's life; only ever used when m_filePath is
     * empty. */
    mutable QString m_untitledKey;
    QVector<int> m_lineStarts;

    QVector<size_t> m_cursors {0}; /* always non-empty, sorted ascending, de-duplicated */
    /* Index-aligned with m_cursors. Equal entries mean no selection;
     * otherwise the range is [min, max) of the pair. */
    QVector<size_t> m_selectionAnchors {0};

    /* Empty means no active search. Kept in original case; ASCII case
     * folding never changes a byte's length. See docs/adr/0021. */
    QByteArray m_findNeedle;
    QVector<size_t> m_matches;
    int m_currentMatch = -1;
    /* Whether the match list is being maintained and drawn. The needle
     * outlives it, so `n` works with nothing on screen. */
    bool m_findActive = false;
    bool m_searchForward = true;
    /* `*` matches whole words; `/` and the find bar do not. */
    bool m_findWholeWord = false;
    FindBar *m_findBar = nullptr;
    FileBrowserPanel *m_fileBrowser = nullptr;
    CommandLine *m_commandLine = nullptr;
    /* Where an inferred command said to run — see docs/adr/0147. */
    QString m_pendingBuildDirectory;
    OutputPanel *m_outputPanel = nullptr;
    HelpPanel *m_helpPanel = nullptr;
    AboutPanel *m_aboutPanel = nullptr;
    AseProcess *m_compileProcess = nullptr;
    QTimer *m_compilePollTimer;

    /* Null means no LSP for this buffer; every LSP method checks. */
    /* Borrowed from the registry, never owned. */
    AseLspClient *m_lspClient = nullptr;
    /* QPointer, not a raw one: both this and the registry are children
     * of the window, and Qt's teardown order is not ours to assume — a
     * dangling registry here would be a crash on exit. */
    QPointer<LspRegistry> m_lspRegistry;
    bool m_lspActivated = false; /* see onActivated() */
    LspState m_lspState = LspState::NotApplicable;
    QString m_lspLanguageId; /* "c" or "cpp"; empty if no server */
    QString m_lspServerName; /* basename, for the status bar */
    QString m_lspUri;
    int m_lspVersion = 1; /* didOpen implicitly sends version 1; didChange starts at 2 */
    QVector<GuiDiagnostic> m_diagnostics;
    QColor m_diagnosticErrorColor;
    QColor m_diagnosticWarningColor;
    /* The two departures from ADR 0007's one-font-colour pillar. These
     * defaults must match ase_config_create_default(). */
    QColor m_syntaxTypeColor {0x68, 0x9d, 0x6a};
    QColor m_syntaxStringColor {0xd7, 0x99, 0x21};
    /* What the captures without a hue vary instead — see docs/adr/0140. */
    bool m_syntaxKeywordBold = true;
    bool m_syntaxTypeItalic = false;
    int m_syntaxCommentAlpha = 145;
    int m_syntaxNumberAlpha = 200;
    /* A couple of entries at most: one wiping in, one wiping out. */
    QVector<DiagnosticLineHighlight> m_diagnosticLineHighlights;

    /* No request-sequence tracking: an out-of-order response can only
     * show a one-keystroke-stale list, which the next one corrects. */
    CompletionPopup *m_completionPopup = nullptr;
    size_t m_completionPrefixStart = 0;
    /* Set by acceptCompletion(), consumed by the very next trigger:
     * accepting an item must not reopen the list on it. */
    bool m_suppressNextCompletionTrigger = false;

    /* Hover — see docs/adr/0030. */
    HoverPanel *m_hoverPanel = nullptr;
    QTimer *m_hoverTimer;
    QPoint m_hoverPendingPos;     /* viewport-local pixel pos the pending/last request was for */
    size_t m_hoverPendingOffset = 0;
    /* What the shown tooltip covers, so scheduleHoverRequest can tell
     * "same word" from "moved" without the server's range. */
    size_t m_hoverShownRangeStart = 0;
    size_t m_hoverShownRangeEnd = 0;

    int m_scrollLine = 0;
    int m_scrollX = 0; /* leftmost visible pixel, not column — see docs/adr/0014 */
    /* The column each caret is aiming for while moving vertically, and the
     * offset that answer was true at. A caret found somewhere else has been
     * moved by something other than j/k, so its column is recomputed — see
     * docs/adr/0139. */
    QVector<int> m_desiredColumns;
    QVector<size_t> m_desiredColumnAt;
    QString m_lineNumberMode = QStringLiteral("absolute"); /* "off" / "absolute" / "relative" */

    /* Defaulting m_vimMode to Insert makes "off" and "on, in Insert"
     * identical everywhere but the keyPressEvent gate. */
    bool m_vimModeEnabled = false;
    VimMode m_vimMode = VimMode::Insert;
    /* The half-typed command — counts, operator, and whatever letter a
     * prefix is waiting on. Cleared as a unit by resetVimPendingState(). */
    VimPending m_vimPending;
    /* 'm' awaiting a letter to set, '`' or '\'' awaiting one to jump to. */
    /* 'q' awaiting a register to record into, '@' awaiting one to play. */
    /* 'i' or 'a' awaiting the object's name. */
    /* Named by `"x` and consumed by the next yank, delete or paste. */
    /* True between `"` and the letter that names the register. */
    /* The operator helpers reset the pending state before they run, so
     * the name is carried across in this. See docs/adr/0105. */
    char m_vimRegisterInUse = '\0';

    /* Enough to replay faithfully: Escape and Backspace carry no text,
     * so a macro of plain characters would lose them. */
    struct RecordedKey {
        int key = 0;
        Qt::KeyboardModifiers mods = Qt::NoModifier;
        QString text;
    };
    QHash<char, QVector<RecordedKey>> m_macros;
    QVector<RecordedKey> m_macroBuffer;
    char m_macroRecording = '\0';
    char m_macroLastPlayed = '\0';
    /* Recursion is allowed; running away is not. See docs/adr/0097. */
    int m_macroReplayDepth = 0;
    int m_macroReplayBudget = 0;
    /* Buffer-local, keyed by letter, as (line, column) 1-based — the
     * same shape the jumplist stores. See docs/adr/0097. */
    QHash<char, QPair<int, int>> m_vimMarks;
    bool m_vimReplacing = false;      /* in `R` Replace mode */
    bool m_undoSessionOpen = false;   /* an insert session owns the group */
    /* What each typed character overwrote, newest last, so Backspace can
     * put it back. An empty entry means that character was appended past
     * the end of the line and there is nothing to restore. */
    QVector<QByteArray> m_replaceOriginals;
    int m_replaceCount = 1;           /* the count given to `R` */
    QByteArray m_replaceTyped;        /* this session's text, for the count */
    /* Last f/F/t/T, for `;` and `,`. Per window, not per line. */
    char m_vimLastFindCommand = '\0';
    char m_vimLastFindTarget = '\0';
    /* Keys of the command being typed now, and of the last one that
     * changed anything. m_dotInserted is what was typed in that change's
     * insert session, if it had one. */
    QString m_dotRecording;
    QString m_dotKeys;
    QByteArray m_dotInserted;
    QByteArray m_dotInsertBuf;
    bool m_dotCapturingInsert = false;
    /* Suppresses recording while replaying, so a repeat never rewrites
     * the change it is repeating. */
    bool m_dotReplaying = false;
    /* Set while a vim remap's right-hand side is being replayed, so the
     * replayed keys are not themselves remapped. This is the "nore" in
     * nnoremap, and without it `vim.normal.x = dd` plus
     * `vim.normal.d = x` is an infinite loop. See docs/adr/0134. */
    bool m_vimRemapping = false;
    /* A flag on Visual, not a fourth mode: everything already works
     * off the selection range. See docs/adr/0056. */
    bool m_vimVisualLinewise = false;
    int m_vimVisualAnchorLine = 0;
    /* Tracked separately: normalizing parks the real cursor at the
     * start of the *next* line, so re-deriving would read one too far
     * and compound on every motion. */
    int m_vimVisualCursorLine = 0;

    /* Easing counterparts of m_scrollLine/X and m_cursors. */
    double m_renderedScrollLine = 0.0;
    double m_renderedScrollX = 0.0;
    QVector<QPointF> m_renderedCaretPos;

    /* Visual only: [start, start+length) just got inserted and scales
     * in. Populated only with animations on. See docs/adr/0049. */
    struct TypingAnimation {
        size_t start;
        size_t length;
        int elapsedTicks;
    };
    QVector<TypingAnimation> m_typingAnimations;
    /* Eased 0..1 — see docs/adr/0042. */
    double m_welcomeOverlayOpacity = 0.0;
    /* A greeting, not a state display — armed by the window for the
     * startup buffer only, disarmed on the first keystroke. */
    bool m_welcomeEligible = false;

    QFont m_font;
    /* So rebuildFont() needn't re-read config. */
    QString m_fontFamily;
    /* 0 = follow config's font_size; otherwise the Ctrl+=/Ctrl+- size. */
    int m_fontSizeOverride = 0;
    int m_lineHeight = 0;
    int m_charWidth = 0;
    /* One per capture-style variant, cached in applyConfig(). */
    QFontMetrics m_metrics {m_font};
    QFontMetrics m_boldMetrics {m_font};
    QFontMetrics m_italicMetrics {m_font};

    QTimer *m_blinkTimer = nullptr;
    bool m_caretVisible = true;
    /* Ticks since the last cursor-moving action; drives both the hard
     * blink and the fade. */
    int m_idleTicks = 0;
};

#endif /* ASE_EDITOR_VIEWPORT_H */

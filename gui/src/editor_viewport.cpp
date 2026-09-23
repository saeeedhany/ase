#include "editor_viewport.h"

#include "motion.h"

#include "about_panel.h"
#include "command_line.h"
#include "file_browser_panel.h"
#include "find_bar.h"
#include "help_panel.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <QDir>
#include <QFileInfo>
#include <QThread>
#include <QTimer>

namespace {
void collectHighlightSpan(void *user_data, AseHighlightSpan span) {
    static_cast<QVector<AseHighlightSpan> *>(user_data)->push_back(span);
}
} // namespace

EditorViewport::EditorViewport(AseBuffer *buffer, QString filePath, QWidget *parent)
    : QWidget(parent), m_buffer(buffer), m_filePath(std::move(filePath)) {
    setFocusPolicy(Qt::StrongFocus);
    setContextMenuPolicy(Qt::NoContextMenu);
    setAutoFillBackground(false);
    setAccessibleName(QStringLiteral("Editor"));
    setAccessibleDescription(QStringLiteral("Text editing area"));

    loadConfig();

    /* Here, not in applyConfig(): that also runs on hot-reload, and
     * would yank a typing user back to Normal. */
    if (m_vimModeEnabled) {
        m_vimMode = VimMode::Normal;
    }

    rebuildSyntax();

    refreshCache();

    m_undo = ase_undo_create();

    /* <config dir>/recovery/, alongside plugins/. Snapshots live here
     * rather than beside the user's files so they never turn up in a
     * project directory or a commit. See docs/adr/0110. */
    if (!m_configPath.isEmpty()) {
        m_recoveryDir = QFileInfo(m_configPath).dir().filePath(QStringLiteral("recovery"));
    }
    /* Restarted by every edit, so the snapshot is written once typing
     * pauses rather than on the keystroke. */
    m_recoveryTimer = new QTimer(this);
    m_recoveryTimer->setSingleShot(true);
    connect(m_recoveryTimer, &QTimer::timeout, this, [this]() { writeRecoverySnapshot(); });

    /* <config dir>/plugins/. A missing directory is not an error. */
    m_pluginHost = ase_plugin_host_create();
    if (m_pluginHost != nullptr && !m_configPath.isEmpty()) {
        QString pluginDir = QFileInfo(m_configPath).dir().filePath(QStringLiteral("plugins"));
        ase_plugin_host_load_directory(m_pluginHost, pluginDir.toUtf8().constData());
    }

    m_pluginEventTimer = new QTimer(this);
    m_pluginEventTimer->setSingleShot(true);
    m_pluginEventTimer->setInterval(50);
    connect(m_pluginEventTimer, &QTimer::timeout, this, [this]() { flushPluginEvents(); });

    /* Deferred, so a hook is not handed a viewport whose constructor has
     * not finished. */
    QTimer::singleShot(0, this, [this]() { emitPluginEvent(ASE_EVENT_FILE_OPENED); });

    m_blinkTimer = new QTimer(this);
    connect(m_blinkTimer, &QTimer::timeout, this, [this]() {
        m_idleTicks++;
        if (m_animationsEnabled) {
            /* The tick runs at frame rate so a scroll is smooth the
             * moment it starts. Once nothing is moving, only the caret's
             * slow breathe is left, which does not need every frame —
             * and a full repaint that often is real idle cost. */
            bool moving = m_renderedScrollLine != m_scrollLine || m_renderedScrollX != m_scrollX ||
                          !m_typingAnimations.isEmpty() ||
                          m_idleTicks < motion::ticksFor(400);
            if (moving || m_idleTicks % motion::ticksFor(32) == 0) {
                update(); /* the fade's phase is also idle-tick-driven — see docs/adr/0017 */
            }
        } else if (m_idleTicks % motion::ticksFor(360) == 0) { /* see docs/adr/0016, docs/adr/0027 */
            m_caretVisible = !m_caretVisible;
            update();
        }
    });
    motion::refreshTickFromScreen(this);
    m_blinkTimer->start(motion::tickMs());
 /* the heartbeat every frame-driven value steps on */

    m_configTimer = new QTimer(this);
    connect(m_configTimer, &QTimer::timeout, this, [this]() { checkConfigReload(); });
    m_configTimer->start(750); /* see docs/adr/0008, decision 4 */

    m_compilePollTimer = new QTimer(this);
    connect(m_compilePollTimer, &QTimer::timeout, this, [this]() { pollCompile(); });

    /* Once at open; after that, on every save. */
    refreshVcsMarks();

    /* The server starts on first activation — see onActivated(). */

    /* So mouseMoveEvent fires with no button held, for hover. */
    setMouseTracking(true);
    m_hoverTimer = new QTimer(this);
    m_hoverTimer->setSingleShot(true);
    connect(m_hoverTimer, &QTimer::timeout, this, [this]() { requestHoverNow(); });
}

/* A language with no grammar still highlights nothing, but it is the
 * same answer the LSP gate gets — see docs/adr/0086. */
void EditorViewport::rebuildSyntax() {
    stopSyntaxWorker();
    ase_syntax_destroy(m_syntax);
    m_syntax = nullptr;
    m_syntaxOverSizeCap = false;

    const char *language =
        ase_config_language_for_path(m_config, m_filePath.toUtf8().constData());
    if (language == nullptr) {
        return;
    }

    /*
     * The cap used to be about time: tree-sitter parses the whole file
     * however little is on screen, and 1.07s at 4.5MB on the UI thread
     * is a freeze (ADR 0107). The parse is on a worker now, so what is
     * left to bound is memory — measured at ~26x the source, 61MB
     * against 114MB for the same 2MB file parsed and not. Hence a
     * higher default and a different reason for it. See docs/adr/0135.
     */
    long capKb = ase_config_get_int(m_config, "syntax_max_kb", 4096);
    if (capKb > 0 && ase_buffer_length(m_buffer) > static_cast<size_t>(capKb) * 1024) {
        m_syntaxOverSizeCap = true;
        return;
    }
    /* Only the languages a grammar was compiled in for; everything else
     * the resolver names still gets a server, just no colours. */
    static const struct {
        const char *name;
        AseLanguage language;
    } kGrammars[] = {
        {"c", ASE_LANG_C},         {"cpp", ASE_LANG_CPP},
        {"python", ASE_LANG_PYTHON}, {"javascript", ASE_LANG_JAVASCRIPT},
        {"css", ASE_LANG_CSS},     {"html", ASE_LANG_HTML},
        {"lua", ASE_LANG_LUA},
    };
    for (const auto &entry : kGrammars) {
        if (strcmp(language, entry.name) != 0) {
            continue;
        }
        /*
         * Big enough that parsing it would be felt, so it goes to a
         * thread that is not drawing. Below this it stays here, where
         * the answer is immediate and there is nothing to coordinate —
         * a 13KB file's colours arriving a frame late would be a
         * regression, not a fix. See docs/adr/0135.
         */
        if (ase_buffer_length(m_buffer) > static_cast<size_t>(kSyncHighlightBytes)) {
            startSyntaxWorker(entry.language);
        } else {
            m_syntax = ase_syntax_create(entry.language);
        }
        return;
    }
}

/*
 * The worker is created on the thread that will run it — moveToThread
 * moves the object, and creating the AseSyntax in its constructor would
 * build a parser on this thread for another one to use. It builds it on
 * its first parse instead.
 */
void EditorViewport::startSyntaxWorker(AseLanguage language) {
    /* Not parented to this: the thread has to outlive the viewport long
     * enough to finish what it is doing — see stopSyntaxWorker(). */
    m_syntaxThread = new QThread;
    m_syntaxWorker = new SyntaxWorker(language);
    m_syntaxWorker->moveToThread(m_syntaxThread);
    connect(m_syntaxThread, &QThread::finished, m_syntaxWorker, &QObject::deleteLater);
    connect(m_syntaxThread, &QThread::finished, m_syntaxThread, &QObject::deleteLater);
    connect(m_syntaxWorker, &SyntaxWorker::parsed, this, &EditorViewport::applyWorkerSpans);
    m_syntaxThread->start();
}

/*
 * Let go without waiting.
 *
 * Waiting was the obvious thing and it cost what the worker was
 * supposed to save: closing a 603KB buffer blocked for 125ms, and a 4MB
 * one would block for about a second — a freeze on close instead of a
 * freeze on open.
 *
 * Nothing is shared to wait for. The worker owns its own AseSyntax, and
 * the text it holds is a QByteArray, whose refcount is atomic — it
 * keeps the bytes alive on its own however soon this object dies.
 * Disconnecting first means the result it is midway through computing
 * is simply not delivered, which is what dropping it would do anyway.
 */
void EditorViewport::stopSyntaxWorker() {
    if (m_syntaxThread == nullptr) {
        return;
    }
    disconnect(m_syntaxWorker, &SyntaxWorker::parsed, this, &EditorViewport::applyWorkerSpans);
    m_syntaxThread->quit();
    /* Both delete themselves once the loop has stopped, via the
     * finished connections made above. */
    m_syntaxThread = nullptr;
    m_syntaxWorker = nullptr;
    m_syntaxBusy = false;
    m_syntaxWantsAnother = false;
}

void EditorViewport::requestAsyncHighlight(int windowStart, int windowEnd) {
    if (m_syntaxWorker == nullptr) {
        return;
    }
    if (m_syntaxBusy) {
        /* Replaced, not queued: the older request is already answering
         * a question about a buffer that has changed. */
        m_syntaxWantsAnother = true;
        m_syntaxPendingStart = windowStart;
        m_syntaxPendingEnd = windowEnd;
        return;
    }
    m_syntaxBusy = true;
    /* m_cache is implicitly shared, so this hands over a pointer and a
     * refcount. The next edit reassigns m_cache and leaves the worker
     * holding what it was given. */
    QMetaObject::invokeMethod(m_syntaxWorker, "parse", Qt::QueuedConnection,
                               Q_ARG(QByteArray, m_cache), Q_ARG(quint64, m_syntaxVersion),
                               Q_ARG(int, windowStart), Q_ARG(int, windowEnd));
}

void EditorViewport::applyWorkerSpans(const QVector<AseHighlightSpan> &spans, quint64 version,
                                       int windowStart, int windowEnd) {
    m_syntaxBusy = false;

    if (version == m_syntaxVersion) {
        /* Same shape as the synchronous path: clear the window, fill it
         * from the spans, and remember what it covers. */
        m_highlights = spans;
        windowStart = std::clamp(windowStart, 0, static_cast<int>(m_captureAt.size()));
        windowEnd = std::clamp(windowEnd, windowStart, static_cast<int>(m_captureAt.size()));
        for (int i = windowStart; i < windowEnd; ++i) {
            m_captureAt[i] = static_cast<uint8_t>(ASE_HL_NONE);
        }
        for (const AseHighlightSpan &span : m_highlights) {
            int spanStart = std::clamp(static_cast<int>(span.start), windowStart, windowEnd);
            int spanEnd = std::clamp(static_cast<int>(span.end), windowStart, windowEnd);
            for (int i = spanStart; i < spanEnd; ++i) {
                m_captureAt[i] = static_cast<uint8_t>(span.capture);
            }
        }
        m_captureWindowStart = windowStart;
        m_captureWindowEnd = windowEnd;
        m_highlightDeferred = false;
        update();
    }

    if (m_syntaxWantsAnother) {
        m_syntaxWantsAnother = false;
        requestAsyncHighlight(m_syntaxPendingStart, m_syntaxPendingEnd);
    }
}

EditorViewport::~EditorViewport() {
    /* A snapshot outlives only a crash. Closing normally means the work
     * was saved or the user chose to drop it, and either way there is
     * nothing to recover. */
    discardRecovery();
    ase_editor_context_destroy(m_pluginContext);
    ase_plugin_host_destroy(m_pluginHost);
    releaseLspClient();
    ase_process_destroy(m_compileProcess);
    ase_process_destroy_detached(m_vcsProcess);
    ase_vcs_diff_destroy(m_vcsDiff);
    stopSyntaxWorker();
    ase_syntax_destroy(m_syntax);
    ase_config_destroy(m_config);
    ase_undo_destroy(m_undo);
    ase_buffer_destroy(m_buffer);
}

void EditorViewport::refreshCache() {
    m_bufferChangedSinceEmit = true;
    schedulePluginEvents();

    size_t len = ase_buffer_length(m_buffer);
    m_cache.resize(static_cast<qsizetype>(len));
    if (len > 0) {
        ase_buffer_get_text(m_buffer, 0, len, m_cache.data());
    }

    /* memchr, not a byte loop: the obvious loop measured 8.2ms on a
     * 276KB file, per keystroke. resize(0) keeps the capacity. */
    m_lineStarts.resize(0);
    m_lineStarts.reserve(m_cache.size() / 24 + 16);
    m_lineStarts.push_back(0);
    const char *data = m_cache.constData();
    const char *cursor = data;
    const char *end = data + m_cache.size();
    while (cursor < end) {
        const char *newline = static_cast<const char *>(memchr(cursor, '\n', end - cursor));
        if (newline == nullptr) {
            break;
        }
        m_lineStarts.push_back(static_cast<int>(newline - data) + 1);
        cursor = newline + 1;
    }

    /* Sized here, filled by ensureCaptureWindow() for the part that is
     * about to be drawn. */
    m_captureAt.fill(static_cast<uint8_t>(ASE_HL_NONE), m_cache.size());
    /* Now, not at the next paint: xForColumn() also runs on the
     * animation tick, and an empty window misplaces the caret. */
    m_captureWindowStart = 0;
    m_captureWindowEnd = 0;
    /* One keystroke costs 0.8ms of re-parse at 10k lines and 20ms at
     * 155k, because an incremental parse still walks a tree that size.
     * Under the threshold it runs now, so colours never lag; over it the
     * parse waits for a pause in typing and the text is drawn plain
     * until it lands. See docs/adr/0107. */
    /* Every edit invalidates whatever a parse in flight is about to
     * answer, so the version it carries stops matching. */
    m_syntaxVersion++;

    /*
     * Deferred now means "a worker is doing it", not "a timer will".
     * Nothing is debounced: the parse is not on this thread, so there
     * is nothing to protect from it, and requestAsyncHighlight() keeps
     * exactly one in flight however fast the typing is. The window
     * stays empty until the answer lands, so the text draws plain and
     * the caret is measured plain to match — the invariant ADR 0124
     * established. See docs/adr/0135.
     */
    m_highlightDeferred = m_syntaxWorker != nullptr;
    if (m_syntax != nullptr || m_syntaxWorker != nullptr) {
        int visibleStart = 0;
        int visibleEnd = 0;
        visibleByteRange(&visibleStart, &visibleEnd);
        ensureCaptureWindow(visibleStart, visibleEnd, true);
    }

    recomputeMatches();
    sendLspDidChange();
    requestCompletionIfAppropriate();
    armRecoverySnapshot();
}

/* Whole-file highlighting cost 14,402 spans and 64ms on a 10,800-line
 * file to draw forty lines. The parse still covers everything; only the
 * query is windowed, padded so ordinary scrolling doesn't re-run it.
 * See docs/adr/0072. */
void EditorViewport::visibleByteRange(int *startByte, int *endByte) const {
    int lineCount = static_cast<int>(m_lineStarts.size());
    int firstLine = std::clamp(static_cast<int>(m_renderedScrollLine), 0, std::max(0, lineCount - 1));
    int lines = (m_lineHeight > 0) ? (height() / m_lineHeight + 2) : 1;
    int lastLine = std::min(firstLine + std::max(1, lines), lineCount);
    *startByte = m_lineStarts.isEmpty() ? 0 : m_lineStarts[firstLine];
    *endByte = (lastLine < lineCount) ? m_lineStarts[lastLine] : static_cast<int>(m_cache.size());
}

void EditorViewport::ensureCaptureWindowForViewport() {
    int startByte = 0;
    int endByte = 0;
    visibleByteRange(&startByte, &endByte);
    ensureCaptureWindow(startByte, endByte, false);
}

void EditorViewport::ensureCaptureWindow(int startByte, int endByte, bool force) {
    if (m_cache.isEmpty() || (m_syntax == nullptr && m_syntaxWorker == nullptr)) {
        return;
    }
    if (!force && startByte >= m_captureWindowStart && endByte <= m_captureWindowEnd) {
        return;
    }

    /* About a screenful either side, so paging usually stays inside.
     * Computed before the split, because both modes want the same
     * window — the only difference is who parses it. */
    const int pad = std::max(4096, (endByte - startByte) * 2);
    const int paddedStart = std::max(0, startByte - pad);
    const int paddedEnd = std::min(static_cast<int>(m_cache.size()), endByte + pad);

    if (m_syntaxWorker != nullptr) {
        /* Asked for, not waited for. The window stays as it is until
         * the answer arrives, which for a file this size is the whole
         * point. See docs/adr/0135. */
        requestAsyncHighlight(paddedStart, paddedEnd);
        return;
    }
    /* Zeroing the window above put every unforced caller back here, so
     * the caret measurement alone re-parsed the whole file on each
     * keystroke and the debounce bought nothing. While a parse is
     * pending the window stays empty: text draws plain, and the caret is
     * measured plain to match. See docs/adr/0124. */
    if (!force && m_highlightDeferred) {
        return;
    }

    const int windowStart = paddedStart;
    const int windowEnd = paddedEnd;

    m_highlights.clear();
    ase_syntax_highlight_range(m_syntax, m_cache.constData(), static_cast<size_t>(m_cache.size()),
                                static_cast<size_t>(windowStart), static_cast<size_t>(windowEnd),
                                collectHighlightSpan, &m_highlights);

    /* Only the window is cleared and refilled. Bytes outside keep what
     * an earlier window left, so an off-window read returns plausible
     * but wrong widths rather than failing — callers must bring the
     * window to their lines first. */
    for (int i = windowStart; i < windowEnd; ++i) {
        m_captureAt[i] = static_cast<uint8_t>(ASE_HL_NONE);
    }
    for (const AseHighlightSpan &span : m_highlights) {
        int spanStart = std::clamp(static_cast<int>(span.start), windowStart, windowEnd);
        int spanEnd = std::clamp(static_cast<int>(span.end), windowStart, windowEnd);
        for (int i = spanStart; i < spanEnd; ++i) {
            m_captureAt[i] = static_cast<uint8_t>(span.capture);
        }
    }

    m_captureWindowStart = windowStart;
    m_captureWindowEnd = windowEnd;
}

int EditorViewport::lineForOffset(size_t offset) const {
    auto it = std::upper_bound(m_lineStarts.begin(), m_lineStarts.end(), static_cast<int>(offset));
    return static_cast<int>(std::distance(m_lineStarts.begin(), it)) - 1;
}

int EditorViewport::columnForOffset(size_t offset, int line) const {
    return static_cast<int>(offset) - m_lineStarts[line];
}

size_t EditorViewport::offsetForLineColumn(int line, int column) const {
    line = std::clamp(line, 0, static_cast<int>(m_lineStarts.size()) - 1);
    int start = m_lineStarts[line];
    int end = (line + 1 < m_lineStarts.size()) ? m_lineStarts[line + 1] - 1 : static_cast<int>(m_cache.size());
    column = std::clamp(column, 0, end - start);
    return static_cast<size_t>(start + column);
}

size_t EditorViewport::offsetForPoint(const QPoint &pos) const {
    /* The rendered, still-easing position: a click during a scroll has
     * to map against what is on screen now. */
    int line = static_cast<int>(std::floor(m_renderedScrollLine)) + pos.y() / std::max(1, m_lineHeight);
    line = std::clamp(line, 0, static_cast<int>(m_lineStarts.size()) - 1);
    /* Into the same local space paintEvent's translate uses. */
    int localX = std::max(0, static_cast<int>(pos.x() - gutterWidth() + m_renderedScrollX));
    /* columnForX measures runs as drawLine paints them, and already
     * rounds to the nearest column and snaps to a codepoint boundary —
     * so no continuation-byte fixup is needed here. */
    int lineStart = m_lineStarts[line];
    int lineEnd =
        (line + 1 < m_lineStarts.size()) ? m_lineStarts[line + 1] - 1 : static_cast<int>(m_cache.size());
    int col = columnForX(lineStart, lineEnd, localX);
    return offsetForLineColumn(line, col);
}

bool EditorViewport::isModalPanelOpen() const {
    return (m_findBar != nullptr && m_findBar->isVisible()) ||
           (m_fileBrowser != nullptr && m_fileBrowser->isVisible()) ||
           (m_commandLine != nullptr && m_commandLine->isPromptOpen()) ||
           (m_helpPanel != nullptr && m_helpPanel->isVisible()) ||
           (m_aboutPanel != nullptr && m_aboutPanel->isVisible());
}

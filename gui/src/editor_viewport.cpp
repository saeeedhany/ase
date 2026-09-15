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

    /* <config dir>/plugins/. A missing directory is not an error. */
    m_pluginHost = ase_plugin_host_create();
    if (m_pluginHost != nullptr && !m_configPath.isEmpty()) {
        QString pluginDir = QFileInfo(m_configPath).dir().filePath(QStringLiteral("plugins"));
        ase_plugin_host_load_directory(m_pluginHost, pluginDir.toUtf8().constData());
    }

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
    ase_syntax_destroy(m_syntax);
    m_syntax = nullptr;

    const char *language =
        ase_config_language_for_path(m_config, m_filePath.toUtf8().constData());
    if (language == nullptr) {
        return;
    }
    if (strcmp(language, "c") == 0) {
        m_syntax = ase_syntax_create(ASE_LANG_C);
    } else if (strcmp(language, "cpp") == 0) {
        m_syntax = ase_syntax_create(ASE_LANG_CPP);
    }
}

EditorViewport::~EditorViewport() {
    ase_plugin_host_destroy(m_pluginHost);
    releaseLspClient();
    ase_process_destroy(m_compileProcess);
    ase_syntax_destroy(m_syntax);
    ase_config_destroy(m_config);
    ase_undo_destroy(m_undo);
    ase_buffer_destroy(m_buffer);
}

void EditorViewport::refreshCache() {
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
    int visibleStart = 0;
    int visibleEnd = 0;
    visibleByteRange(&visibleStart, &visibleEnd);
    ensureCaptureWindow(visibleStart, visibleEnd, true);

    recomputeMatches();
    sendLspDidChange();
    requestCompletionIfAppropriate();
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
    if (m_syntax == nullptr || m_cache.isEmpty()) {
        return;
    }
    if (!force && startByte >= m_captureWindowStart && endByte <= m_captureWindowEnd) {
        return;
    }

    /* About a screenful either side, so paging usually stays inside. */
    int pad = std::max(4096, (endByte - startByte) * 2);
    int windowStart = std::max(0, startByte - pad);
    int windowEnd = std::min(static_cast<int>(m_cache.size()), endByte + pad);

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

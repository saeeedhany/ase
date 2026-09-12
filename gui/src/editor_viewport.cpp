#include "editor_viewport.h"

#include "motion.h"

#include "about_panel.h"
#include "command_line.h"
#include "file_browser_panel.h"
#include "find_bar.h"
#include "help_panel.h"

#include <algorithm>
#include <cmath>

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

    /* Real vim starts in Normal mode, not Insert — a one-time startup
     * decision made here, not inside applyConfig() itself (which also
     * runs on every config hot-reload; doing it there would yank an
     * actively-typing user back to Normal just because config.ase's
     * mtime changed for some unrelated edit). See docs/adr/0050. */
    if (m_vimModeEnabled) {
        m_vimMode = VimMode::Normal;
    }

    QString suffix = QFileInfo(m_filePath).suffix().toLower();
    if (suffix == QLatin1String("c") || suffix == QLatin1String("h")) {
        m_syntax = ase_syntax_create_c();
    }

    refreshCache();

    m_undo = ase_undo_create();

    /* Plugins live next to config.ase, in <config dir>/plugins/. A
     * missing directory is not an error (the host returns 0 loaded), so
     * there's nothing to configure for anyone who has no plugins — same
     * "unconfigured is a normal state, not a failure" stance the LSP and
     * build commands already take. See docs/adr/0054. */
    m_pluginHost = ase_plugin_host_create();
    if (m_pluginHost != nullptr && !m_configPath.isEmpty()) {
        QString pluginDir = QFileInfo(m_configPath).dir().filePath(QStringLiteral("plugins"));
        ase_plugin_host_load_directory(m_pluginHost, pluginDir.toUtf8().constData());
    }

    m_blinkTimer = new QTimer(this);
    connect(m_blinkTimer, &QTimer::timeout, this, [this]() {
        m_idleTicks++;
        if (m_animationsEnabled) {
            update(); /* the fade's phase is also idle-tick-driven — see docs/adr/0017 */
        } else if (m_idleTicks % 12 == 0) { /* ~360ms at this 30ms tick — was 17 (~500ms), see docs/adr/0016, docs/adr/0027 */
            m_caretVisible = !m_caretVisible;
            update();
        }
    });
    m_blinkTimer->start(motion::kTickMs); /* the heartbeat every frame-driven value steps on */

    m_configTimer = new QTimer(this);
    connect(m_configTimer, &QTimer::timeout, this, [this]() { checkConfigReload(); });
    m_configTimer->start(750); /* see docs/adr/0008, decision 4 */

    m_compilePollTimer = new QTimer(this);
    connect(m_compilePollTimer, &QTimer::timeout, this, [this]() { pollCompile(); });

    m_lspPollTimer = new QTimer(this);
    connect(m_lspPollTimer, &QTimer::timeout, this, [this]() { pollLsp(); });
    m_lspPollTimer->start(200); /* non-blocking poll, same shape as config-reload/compile-output polling */
    /* The server itself starts on first activation, not here — see
     * onActivated() and docs/adr/0054. */

    /* Needed for mouseMoveEvent to fire with no button held — hover
     * (docs/adr/0030) has to track the pointer passively, not just
     * during a drag. */
    setMouseTracking(true);
    m_hoverTimer = new QTimer(this);
    m_hoverTimer->setSingleShot(true);
    connect(m_hoverTimer, &QTimer::timeout, this, [this]() { requestHoverNow(); });
}

EditorViewport::~EditorViewport() {
    ase_plugin_host_destroy(m_pluginHost);
    ase_lsp_client_stop(m_lspClient);
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

    m_lineStarts.clear();
    m_lineStarts.push_back(0);
    for (int i = 0; i < m_cache.size(); ++i) {
        if (m_cache[i] == '\n') {
            m_lineStarts.push_back(i + 1);
        }
    }

    m_highlights.clear();
    if (m_syntax != nullptr) {
        ase_syntax_highlight(m_syntax, m_cache.constData(), static_cast<size_t>(m_cache.size()),
                              collectHighlightSpan, &m_highlights);
    }

    /* Flattened to one capture byte per buffer byte, once per edit,
     * rather than re-derived per line per frame. capturesForLine() used
     * to scan *every* span in the file for *every* visible line on
     * *every* frame — with animations on (a repaint every 30ms) that is
     * O(visible_lines * spans_in_file) forever, which measured as 22%
     * CPU at complete idle on an 8400-line file. Same "mirror the whole
     * buffer once, index it cheaply after" shape m_cache and
     * m_lineStarts already use (docs/adr/0006). See docs/adr/0053. */
    m_captureAt.assign(m_cache.size(), static_cast<uint8_t>(ASE_HL_NONE));
    for (const AseHighlightSpan &span : m_highlights) {
        int spanStart = std::clamp(static_cast<int>(span.start), 0, static_cast<int>(m_captureAt.size()));
        int spanEnd = std::clamp(static_cast<int>(span.end), 0, static_cast<int>(m_captureAt.size()));
        for (int i = spanStart; i < spanEnd; ++i) {
            m_captureAt[i] = static_cast<uint8_t>(span.capture);
        }
    }

    recomputeMatches();
    sendLspDidChange();
    requestCompletionIfAppropriate();
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
    /* Uses the *rendered* (possibly still-easing) scroll position, not
     * the logical target — a click during an active scroll animation
     * has to map against what's actually on screen right now. See
     * docs/adr/0015. The vertical fractional part is ignored (floor
     * only) — a sub-line-height miss mid-animation is a transient,
     * approximate case, consistent with docs/adr/0013's click-precision
     * philosophy. */
    int line = static_cast<int>(std::floor(m_renderedScrollLine)) + pos.y() / std::max(1, m_lineHeight);
    line = std::clamp(line, 0, static_cast<int>(m_lineStarts.size()) - 1);
    /* Shifted by the gutter and horizontal scroll (docs/adr/0014) to land
     * in the same local coordinate space paintEvent's translate uses. */
    int localX = std::max(0, static_cast<int>(pos.x() - gutterWidth() + m_renderedScrollX));
    /* columnForX measures each run's actual rendered width the same way
     * drawLine paints it, rather than assuming column * m_charWidth. That
     * fixed-pitch assumption used to live here directly and drifted
     * further from the real character the further right a click/hover
     * landed on a line — the mouse-side counterpart of the caret-drift
     * bug docs/adr/0013 already fixed for the caret itself. See
     * docs/adr/0039. columnForX already rounds to the nearest column
     * (docs/adr/0028) and already snaps to a codepoint boundary, so no
     * separate continuation-byte fixup is needed here anymore. */
    int lineStart = m_lineStarts[line];
    int lineEnd =
        (line + 1 < m_lineStarts.size()) ? m_lineStarts[line + 1] - 1 : static_cast<int>(m_cache.size());
    int col = columnForX(lineStart, lineEnd, localX);
    return offsetForLineColumn(line, col);
}

bool EditorViewport::isModalPanelOpen() const {
    return (m_findBar != nullptr && m_findBar->isVisible()) ||
           (m_fileBrowser != nullptr && m_fileBrowser->isVisible()) ||
           (m_commandLine != nullptr && m_commandLine->isVisible()) ||
           (m_helpPanel != nullptr && m_helpPanel->isVisible()) ||
           (m_aboutPanel != nullptr && m_aboutPanel->isVisible());
}

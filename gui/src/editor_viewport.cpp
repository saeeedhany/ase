#include "editor_viewport.h"

#include "about_panel.h"
#include "command_line.h"
#include "completion_popup.h"
#include "file_browser_panel.h"
#include "find_bar.h"
#include "help_panel.h"
#include "hover_panel.h"
#include "output_panel.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include <QClipboard>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QWheelEvent>

namespace {
/* ~720ms period at the 30ms tick below — was 34 (~1020ms); sped up
 * along with every other animation in the app, see docs/adr/0027. */
constexpr int kCaretAnimationTicks = 24;
constexpr double kTwoPi = 6.283185307179586;
constexpr int kCaretWidth = 2;
/* The Normal-mode block cursor's fill, capped well under fully opaque —
 * even at the breathing cycle's brightest instant (see caretAlpha
 * below), a solid 100%-opaque block would flash harder than every
 * other translucent overlay in this app's aesthetic (selection
 * highlight, panel backgrounds, ...) and would fight for attention
 * with the knocked-out glyph drawn on top of it. See docs/adr/0047. */
constexpr int kVimBlockCursorMaxAlpha = 200;
constexpr int kGutterPadding = 8; /* on each side of the line-number text */
/* Per paint — see docs/adr/0015. The single global speed knob for
 * every eased value in this file (caret glide, scroll, diagnostic
 * focus reveal, the welcome overlay's fade — docs/adr/0041,
 * docs/adr/0042): raising it here speeds up all of them together, by
 * construction, since they all multiply their remaining distance by
 * this each frame. Was 0.35, then 0.5 (docs/adr/0027); raised again
 * — caret/scroll glide still read as sluggish on Enter/Delete and
 * general navigation. See docs/adr/0043. */
constexpr double kEaseFactor = 0.68;
/* "scrolloff"-style context margin, in lines/characters, kept visible
 * around the cursor before the view scrolls — see docs/adr/0024. */
constexpr int kVerticalScrollMargin = 3;
constexpr int kHorizontalScrollMarginChars = 4;

/* Diagnostic underline + gutter dot focus states — see docs/adr/0041.
 * The dot's own range starts higher (it was already fully opaque
 * before this feature existed) so diagnostics stay easy to spot in the
 * gutter at a glance even unfocused. Reuses kEaseFactor for the reveal
 * animation itself, for the same snappy convergence as caret
 * glide/scroll (docs/adr/0027). */
constexpr int kDiagnosticUnderlineDimAlpha = 110;
constexpr int kDiagnosticUnderlineFocusAlpha = 255;
constexpr int kDiagnosticDotDimAlpha = 150;
constexpr int kDiagnosticDotFocusAlpha = 255;
/* Shared by any eased-opacity/reveal value in this file (diagnostic
 * focus, the empty-buffer welcome overlay — docs/adr/0042) below which
 * it snaps straight to its target instead of asymptotically crawling
 * forever. */
constexpr double kOpacitySnapThreshold = 0.01;

bool isUtf8ContinuationByte(char byte) {
    return (static_cast<unsigned char>(byte) & 0xC0) == 0x80;
}

bool isWordChar(char c) {
    unsigned char uc = static_cast<unsigned char>(c);
    return (uc >= 'a' && uc <= 'z') || (uc >= 'A' && uc <= 'Z') || (uc >= '0' && uc <= '9') || uc == '_';
}

void collectHighlightSpan(void *user_data, AseHighlightSpan span) {
    static_cast<QVector<AseHighlightSpan> *>(user_data)->push_back(span);
}

/* One piece of a hover response's `contents`: either a plain string, or
 * an object carrying a "value" string (covers both MarkupContent
 * {kind, value} and the legacy single-MarkedString {language, value}
 * shape) — see docs/adr/0030. Anything else yields an empty piece,
 * silently dropped by extractHoverText below. */
QString extractHoverPiece(const AseJsonValue *value) {
    if (value == nullptr) {
        return QString();
    }
    if (ase_json_type(value) == ASE_JSON_STRING) {
        const char *s = ase_json_get_string(value);
        return (s != nullptr) ? QString::fromUtf8(s) : QString();
    }
    if (ase_json_type(value) == ASE_JSON_OBJECT) {
        const char *v = ase_json_get_string(ase_json_object_get(value, "value"));
        return (v != nullptr) ? QString::fromUtf8(v) : QString();
    }
    return QString();
}

/* `contents` may be a single piece or (legacy MarkedString[]) an array
 * of them — joined with a blank line between, same as most editors
 * render multi-part hover. No markdown rendering (v1 simplification,
 * see docs/adr/0030) — shown as plain wrapped text either way. */
QString extractHoverText(const AseJsonValue *contents) {
    if (contents == nullptr) {
        return QString();
    }
    if (ase_json_type(contents) == ASE_JSON_ARRAY) {
        QStringList parts;
        size_t count = ase_json_array_size(contents);
        for (size_t i = 0; i < count; ++i) {
            QString piece = extractHoverPiece(ase_json_array_get(contents, i));
            if (!piece.isEmpty()) {
                parts << piece;
            }
        }
        return parts.join(QStringLiteral("\n\n"));
    }
    return extractHoverPiece(contents);
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

    QString suffix = QFileInfo(m_filePath).suffix().toLower();
    if (suffix == QLatin1String("c") || suffix == QLatin1String("h")) {
        m_syntax = ase_syntax_create_c();
    }

    refreshCache();

    m_undo = ase_undo_create();

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
    m_blinkTimer->start(30);

    m_configTimer = new QTimer(this);
    connect(m_configTimer, &QTimer::timeout, this, [this]() { checkConfigReload(); });
    m_configTimer->start(750); /* see docs/adr/0008, decision 4 */

    m_compilePollTimer = new QTimer(this);
    connect(m_compilePollTimer, &QTimer::timeout, this, [this]() { pollCompile(); });

    m_lspPollTimer = new QTimer(this);
    connect(m_lspPollTimer, &QTimer::timeout, this, [this]() { pollLsp(); });
    m_lspPollTimer->start(200); /* non-blocking poll, same shape as config-reload/compile-output polling */
    startLspClientIfConfigured();

    /* Needed for mouseMoveEvent to fire with no button held — hover
     * (docs/adr/0030) has to track the pointer passively, not just
     * during a drag. */
    setMouseTracking(true);
    m_hoverTimer = new QTimer(this);
    m_hoverTimer->setSingleShot(true);
    connect(m_hoverTimer, &QTimer::timeout, this, [this]() { requestHoverNow(); });
}

EditorViewport::~EditorViewport() {
    ase_lsp_client_stop(m_lspClient);
    ase_process_destroy(m_compileProcess);
    ase_syntax_destroy(m_syntax);
    ase_config_destroy(m_config);
    ase_undo_destroy(m_undo);
    ase_buffer_destroy(m_buffer);
}

void EditorViewport::loadConfig() {
    char *path = ase_config_default_path();
    if (path != nullptr) {
        m_configPath = QString::fromLocal8Bit(path);
        free(path);
        ase_config_write_default_if_missing(m_configPath.toUtf8().constData());
    }

    m_config = ase_config_load(m_configPath.isEmpty() ? nullptr : m_configPath.toUtf8().constData());
    applyConfig();

    if (!m_configPath.isEmpty()) {
        m_configModified = QFileInfo(m_configPath).lastModified();
    }
}

void EditorViewport::applyConfig() {
    uint8_t r, g, b, a;
    if (ase_config_get_color(m_config, "background", &r, &g, &b, &a)) {
        m_backgroundColor = QColor(r, g, b, a);
    }
    if (ase_config_get_color(m_config, "text", &r, &g, &b, &a)) {
        m_textColor = QColor(r, g, b, a);
    }
    if (ase_config_get_color(m_config, "selection", &r, &g, &b, &a)) {
        m_selectionColor = QColor(r, g, b, a);
    }
    if (ase_config_get_color(m_config, "find_match", &r, &g, &b, &a)) {
        m_findMatchColor = QColor(r, g, b, a);
    }
    if (ase_config_get_color(m_config, "panel_background", &r, &g, &b, &a)) {
        m_panelBackgroundColor = QColor(r, g, b, a);
    }
    if (ase_config_get_color(m_config, "diagnostic_error", &r, &g, &b, &a)) {
        m_diagnosticErrorColor = QColor(r, g, b, a);
    }
    if (ase_config_get_color(m_config, "diagnostic_warning", &r, &g, &b, &a)) {
        m_diagnosticWarningColor = QColor(r, g, b, a);
    }

    const char *familyStr = ase_config_get_string(m_config, "font_family");
    QString family = familyStr != nullptr ? QString::fromUtf8(familyStr) : QStringLiteral("monospace");
    long size = ase_config_get_int(m_config, "font_size", 12);

    m_font = family.compare(QLatin1String("monospace"), Qt::CaseInsensitive) == 0
                 ? QFontDatabase::systemFont(QFontDatabase::FixedFont)
                 : QFont(family);
    m_font.setPointSize(static_cast<int>(size));

    /* Cached once here rather than reconstructed per run per paint — see
     * docs/adr/0017. */
    m_metrics = QFontMetrics(m_font);
    QFont boldFont = m_font;
    boldFont.setBold(true);
    m_boldMetrics = QFontMetrics(boldFont);
    QFont italicFont = m_font;
    italicFont.setItalic(true);
    m_italicMetrics = QFontMetrics(italicFont);

    m_lineHeight = m_metrics.height();
    m_charWidth = m_metrics.horizontalAdvance(QLatin1Char('M'));

    /* Opt-in, off by default — see docs/adr/0012, decision 2. */
    const char *animationsStr = ase_config_get_string(m_config, "animations");
    m_animationsEnabled = animationsStr != nullptr &&
                          QString::fromUtf8(animationsStr).compare(QLatin1String("true"), Qt::CaseInsensitive) == 0;

    /* Opt-in, off by default, same reasoning and same pattern as
     * animations above — this changes what every keystroke does, so it
     * has to be asked for. See docs/adr/0046. */
    const char *vimModeStr = ase_config_get_string(m_config, "vim_mode");
    m_vimModeEnabled = vimModeStr != nullptr &&
                       QString::fromUtf8(vimModeStr).compare(QLatin1String("true"), Qt::CaseInsensitive) == 0;

    /* On by default, unlike animations — see docs/adr/0014, decision 4.
     * An unrecognized value falls back to "absolute" rather than
     * treating it as an error — a bad config value should never break
     * the editor. */
    const char *lineNumbersStr = ase_config_get_string(m_config, "line_numbers");
    m_lineNumberMode = lineNumbersStr != nullptr ? QString::fromUtf8(lineNumbersStr).toLower()
                                                  : QStringLiteral("absolute");
    if (m_lineNumberMode != QLatin1String("off") && m_lineNumberMode != QLatin1String("relative")) {
        m_lineNumberMode = QStringLiteral("absolute");
    }
}

void EditorViewport::checkConfigReload() {
    if (m_configPath.isEmpty()) {
        return;
    }

    QDateTime modified = QFileInfo(m_configPath).lastModified();
    if (!modified.isValid() || modified == m_configModified) {
        return;
    }
    m_configModified = modified;

    ase_config_destroy(m_config);
    m_config = ase_config_load(m_configPath.toUtf8().constData());
    applyConfig();
    if (m_findBar != nullptr) {
        m_findBar->refreshTheme();
    }
    if (m_fileBrowser != nullptr) {
        m_fileBrowser->refreshTheme();
    }
    if (m_commandLine != nullptr) {
        m_commandLine->refreshTheme();
    }
    if (m_outputPanel != nullptr) {
        m_outputPanel->refreshTheme();
    }
    if (m_helpPanel != nullptr) {
        m_helpPanel->refreshTheme();
    }
    if (m_aboutPanel != nullptr) {
        m_aboutPanel->refreshTheme();
    }
    if (m_completionPopup != nullptr) {
        m_completionPopup->refreshTheme();
    }
    if (m_hoverPanel != nullptr) {
        m_hoverPanel->refreshTheme();
    }
    ensureCursorVisible();
    update();
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

void EditorViewport::paintEvent(QPaintEvent *) {
    updateAnimation();

    QPainter painter(this);
    painter.fillRect(rect(), m_backgroundColor);

    int gutter = gutterWidth();
    int textAreaWidth = std::max(1, width() - gutter);

    int firstLine = std::max(0, static_cast<int>(std::floor(m_renderedScrollLine)));
    double fracLine = m_renderedScrollLine - std::floor(m_renderedScrollLine);
    /* +2, not +1: the fractional vertical shift below can reveal a line
     * beyond what a plain height()/lineHeight count would cover. */
    int visibleLines = std::max(1, height() / m_lineHeight + 2);
    int lastLine = std::min(firstLine + visibleLines, static_cast<int>(m_lineStarts.size()));

    /* Everything from here to restore() draws in "local" (document)
     * coordinates — the translate folds in the gutter offset, horizontal
     * scroll, and the sub-line-height vertical remainder together, so
     * drawLine/xForColumn didn't need to change at all. The clip keeps
     * any of it from painting over the gutter. See docs/adr/0014
     * (gutter/scroll) and docs/adr/0015 (the fractional part, added for
     * smooth scrolling). */
    painter.save();
    painter.translate(gutter - m_renderedScrollX, -fracLine * m_lineHeight);
    painter.setClipRect(QRectF(m_renderedScrollX, -static_cast<double>(m_lineHeight), textAreaWidth,
                                height() + 2.0 * m_lineHeight));

    /* Selection highlight, drawn behind the glyphs (this loop runs before
     * the text-drawing loop below, same translate/clip) so text stays
     * crisp on top of the translucent overlay — see docs/adr/0019. */
    for (int i = 0; i < m_cursors.size(); ++i) {
        if (!hasSelectionAt(i)) {
            continue;
        }
        highlightRange(painter, selectionMinAt(i), selectionMaxAt(i), firstLine, lastLine, m_selectionColor);
    }

    /* Find/replace matches — same pass, same reasoning. Regular matches
     * reuse the selection color; the current match gets the stronger,
     * dedicated find_match color, drawn last so it lands on top of any
     * regular-match rect it might overlap. See docs/adr/0021. */
    if (!m_matches.isEmpty()) {
        size_t needleLen = static_cast<size_t>(m_findNeedle.size());
        for (int m = 0; m < m_matches.size(); ++m) {
            if (m == m_currentMatch) {
                continue;
            }
            highlightRange(painter, m_matches[m], m_matches[m] + needleLen, firstLine, lastLine, m_selectionColor);
        }
        if (m_currentMatch >= 0) {
            highlightRange(painter, m_matches[m_currentMatch], m_matches[m_currentMatch] + needleLen, firstLine,
                            lastLine, m_findMatchColor);
        }
    }

    for (int line = firstLine; line < lastLine; ++line) {
        int start = m_lineStarts[line];
        int end = (line + 1 < m_lineStarts.size()) ? m_lineStarts[line + 1] - 1 : static_cast<int>(m_cache.size());
        int y = (line - firstLine) * m_lineHeight;
        drawLine(painter, start, end, y);
    }

    /* LSP diagnostic underlines — drawn on top of the glyphs, same
     * translate/clip as the text loop above. `character` is treated as a
     * direct byte offset within the line (ASCII-only v1 simplification,
     * consistent with this codebase's existing byte-level cursor
     * shortcuts — see docs/adr/0029); offsetForLineColumn's clamping
     * also keeps a diagnostic that's gone stale after an edit (line no
     * longer exists) from drawing garbage. */
    for (const GuiDiagnostic &d : m_diagnostics) {
        size_t start = offsetForLineColumn(d.startLine, d.startChar);
        size_t end = offsetForLineColumn(d.endLine, d.endChar);
        if (end <= start) {
            end = start + 1;
        }
        drawDiagnosticUnderline(painter, start, end, firstLine, lastLine, colorForSeverity(d.severity));
    }
    painter.restore();

    /* Carets are drawn separately, in absolute widget space, at their
     * *rendered* (eased) positions — decoupled from the instant scroll
     * target above so a caret mid-glide isn't forced to jump with it.
     * See docs/adr/0015. */
    /* Both branches key off m_idleTicks (reset to 0 by resetCaretBlink on
     * every cursor-moving action), not a free-running counter — so both
     * "the caret must not blink/fade while the user is actively moving
     * it" for either mode. cos (not sin) means phase 0 — i.e. the instant
     * something goes idle — evaluates to full brightness, so the fade
     * always starts from "was solid, now easing into the breathing
     * cycle" rather than jumping to some arbitrary point in the curve.
     * See docs/adr/0017. */
    int caretAlpha = 255;
    if (m_animationsEnabled) {
        double phase = (m_idleTicks % kCaretAnimationTicks) / static_cast<double>(kCaretAnimationTicks);
        caretAlpha = std::clamp(static_cast<int>(128 + 127 * std::cos(phase * kTwoPi)), 0, 255);
    } else if (!m_caretVisible) {
        caretAlpha = 0;
    }

    /* Normal mode gets a real vim-style block cursor — filling the
     * whole character cell, not the thin insertion bar used everywhere
     * else — since otherwise this editor's caret looks identical
     * whether Normal mode is active or not, with no visual cue that
     * every keystroke currently means something completely different.
     * Insert keeps the bar (it still means "an insertion point," same
     * as with Vim mode off); Visual keeps it too, since Visual already
     * highlights the selection range itself and the bar there just
     * marks that selection's moving end without competing with it.
     * See docs/adr/0047. */
    bool blockCursor = vimModeActive() && m_vimMode == VimMode::Normal;

    if (caretAlpha > 0 && !m_renderedCaretPos.isEmpty()) {
        painter.save();
        painter.setClipRect(QRect(gutter, 0, textAreaWidth, height()));
        if (blockCursor) {
            int blockAlpha = (caretAlpha * kVimBlockCursorMaxAlpha) / 255;
            QColor blockColor = m_textColor;
            blockColor.setAlpha(blockAlpha);
            for (int i = 0; i < m_renderedCaretPos.size(); ++i) {
                const QPointF &pos = m_renderedCaretPos[i];
                int width = m_charWidth;
                AseHighlightCapture capture = ASE_HL_NONE;
                QString glyph = vimBlockGlyphAt(m_cursors[i], &width, &capture);

                painter.fillRect(QRectF(pos.x(), pos.y(), width, m_lineHeight), blockColor);
                if (!glyph.isEmpty()) {
                    /* Knocked out in the background color, terminal-
                     * cursor style, so the character underneath stays
                     * legible through the block regardless of the
                     * block's own opacity — otherwise a block cursor
                     * filled in the text's own color would just hide
                     * whatever it lands on. */
                    painter.setFont(fontForCapture(capture));
                    painter.setPen(m_backgroundColor);
                    painter.drawText(QRectF(pos.x(), pos.y(), width, m_lineHeight),
                                      Qt::AlignLeft | Qt::AlignVCenter | Qt::TextDontClip, glyph);
                }
            }
        } else {
            QColor caretColor = m_textColor;
            caretColor.setAlpha(caretAlpha);
            for (const QPointF &pos : m_renderedCaretPos) {
                painter.fillRect(QRectF(pos.x(), pos.y(), kCaretWidth, m_lineHeight), caretColor);
            }
        }
        painter.restore();
    }

    if (gutter > 0) {
        painter.save();
        painter.translate(0, -fracLine * m_lineHeight);
        painter.fillRect(QRectF(0, -m_lineHeight, gutter, height() + 2.0 * m_lineHeight), m_backgroundColor);

        int cursorLine = lineForOffset(m_cursors.last());
        QColor dim = m_textColor;
        dim.setAlpha(145); /* reuses the comment-dimming tier — see docs/adr/0014, decision 4 */
        QColor current = m_textColor;
        current.setAlpha(220);

        painter.setFont(m_font);
        for (int line = firstLine; line < lastLine; ++line) {
            painter.setPen(line == cursorLine ? current : dim);
            int y = (line - firstLine) * m_lineHeight;

            /* Diagnostic gutter dot — drawn in the left padding the
             * right-aligned line number text never reaches (see
             * docs/adr/0029). Dim at rest, fully vivid while its line is
             * focused — "just to be focused," no wipe, unlike the line
             * highlight below (see docs/adr/0041) — sharing the same
             * reveal value so both animate in lockstep. */
            int worstSeverity = worstSeverityForLine(line);
            if (worstSeverity != 0) {
                double reveal = diagnosticRevealForLine(line);
                QColor dotColor = colorForSeverity(worstSeverity);
                dotColor.setAlpha(static_cast<int>(kDiagnosticDotDimAlpha +
                                                    (kDiagnosticDotFocusAlpha - kDiagnosticDotDimAlpha) * reveal));
                painter.save();
                painter.setPen(Qt::NoPen);
                painter.setBrush(dotColor);
                painter.setRenderHint(QPainter::Antialiasing, true);
                constexpr double kDotSize = 4.0;
                painter.drawEllipse(QRectF(2.0, y + (m_lineHeight - kDotSize) / 2.0, kDotSize, kDotSize));
                painter.restore();
            }

            painter.drawText(QRect(0, y, gutter - kGutterPadding, m_lineHeight),
                              Qt::AlignRight | Qt::AlignVCenter | Qt::TextDontClip,
                              gutterLabelForLine(line, cursorLine));
        }
        painter.restore();
    }

    drawWelcomeOverlay(painter);
}

/* Advances the rendered scroll/caret state one step toward its logical
 * target. Called from the top of paintEvent — see docs/adr/0015. */
void EditorViewport::updateAnimation() {
    updateDiagnosticLineHighlights();
    updateWelcomeOverlayOpacity();

    if (!m_animationsEnabled) {
        /* Snap every rendered value to its exact target — NOT clear
         * m_renderedCaretPos. paintEvent only draws carets when that
         * array is non-empty, so clearing it here (an earlier bug)
         * meant the caret never rendered at all with the default
         * animations = false config. */
        snapAnimationToTarget();
        return;
    }

    double deltaLine = m_scrollLine - m_renderedScrollLine;
    m_renderedScrollLine += (std::abs(deltaLine) < 0.02) ? deltaLine : deltaLine * kEaseFactor;

    double deltaX = m_scrollX - m_renderedScrollX;
    m_renderedScrollX += (std::abs(deltaX) < 0.5) ? deltaX : deltaX * kEaseFactor;

    if (m_renderedCaretPos.size() != m_cursors.size()) {
        /* Cursor count just changed (added/removed) — snap to targets
         * this frame rather than gliding from a mismatched old array. */
        m_renderedCaretPos.resize(m_cursors.size());
        for (int i = 0; i < m_cursors.size(); ++i) {
            m_renderedCaretPos[i] = caretTargetFor(m_cursors[i]);
        }
        return;
    }

    for (int i = 0; i < m_cursors.size(); ++i) {
        QPointF target = caretTargetFor(m_cursors[i]);
        QPointF &rendered = m_renderedCaretPos[i];
        QPointF delta = target - rendered;
        if (std::abs(delta.x()) < 0.5 && std::abs(delta.y()) < 0.5) {
            rendered = target;
        } else {
            rendered += delta * kEaseFactor;
        }
    }
}

QPointF EditorViewport::caretTargetFor(size_t cursor) const {
    int line = lineForOffset(cursor);
    int lineStart = m_lineStarts[line];
    int lineEnd = (line + 1 < m_lineStarts.size()) ? m_lineStarts[line + 1] - 1 : static_cast<int>(m_cache.size());
    int col = columnForOffset(cursor, line);
    double x = gutterWidth() - m_renderedScrollX + xForColumn(lineStart, lineEnd, col);
    double y = (line - m_renderedScrollLine) * m_lineHeight;
    return QPointF(x, y);
}

void EditorViewport::resetCaretBlink() {
    m_caretVisible = true;
    m_idleTicks = 0;
}

void EditorViewport::snapAnimationToTarget() {
    m_renderedScrollLine = m_scrollLine;
    m_renderedScrollX = m_scrollX;
    m_renderedCaretPos.resize(m_cursors.size());
    for (int i = 0; i < m_cursors.size(); ++i) {
        m_renderedCaretPos[i] = caretTargetFor(m_cursors[i]);
    }
}

/* One entry per byte in [start, end), naming which capture (if any) that
 * byte belongs to — shared by drawLine and xForColumn so both segment a
 * line into runs identically. */
QVector<AseHighlightCapture> EditorViewport::capturesForLine(int start, int end) const {
    int lineLen = end - start;
    QVector<AseHighlightCapture> captures(std::max(0, lineLen), ASE_HL_NONE);

    for (const AseHighlightSpan &span : m_highlights) {
        int spanStart = static_cast<int>(span.start);
        int spanEnd = static_cast<int>(span.end);
        if (spanEnd <= start || spanStart >= end) {
            continue;
        }
        int clampedStart = std::max(spanStart, start);
        int clampedEnd = std::min(spanEnd, end);
        for (int i = clampedStart; i < clampedEnd; ++i) {
            captures[i - start] = span.capture;
        }
    }
    return captures;
}

/* Splits [start, end) into same-capture runs and draws each with its own
 * style. All captures render in m_textColor's hue — only opacity/weight/style
 * vary — so the query's captures don't need to be mutually exclusive in
 * general, just non-overlapping in practice for the leaf-level nodes
 * c_highlights.scm captures. See docs/adr/0007, decision 1.
 *
 * Each run's on-screen advance is that run's own *measured* rendered
 * width (QFontMetrics::horizontalAdvance on the actual run text, with
 * that run's actual font), not runLen * m_charWidth — see docs/adr/0013
 * for why the latter drifts visibly on longer lines.
 *
 * The drawText rect's width is that same measured runWidth, never
 * width() - x. Once horizontal scroll is in play (docs/adr/0014), x is
 * a *local* (translated) coordinate that can legitimately exceed the
 * widget's raw width() — width() - x then goes negative, and a
 * negative-width QRect makes drawText paint nothing. That was a real
 * bug: characters typed past the point a line had scrolled rendered as
 * blank space. See docs/adr/0017. */
void EditorViewport::drawLine(QPainter &painter, int start, int end, int y) {
    int lineLen = end - start;
    if (lineLen <= 0) {
        return;
    }

    QVector<AseHighlightCapture> captures = capturesForLine(start, end);

    int x = 0;
    int runStart = 0;
    for (int i = 1; i <= lineLen; ++i) {
        if (i < lineLen && captures[i] == captures[runStart]) {
            continue;
        }
        int runLen = i - runStart;
        QString text = QString::fromUtf8(m_cache.constData() + start + runStart, runLen);
        AseHighlightCapture capture = captures[runStart];
        int runWidth = metricsForCapture(capture).horizontalAdvance(text);
        painter.setFont(fontForCapture(capture));
        painter.setPen(colorForCapture(capture));
        /* Qt::TextDontClip: m_lineHeight is measured once from the plain
         * (non-bold, non-italic) font (see applyConfig()), but italic —
         * and to a lesser extent bold — variants of the same font can
         * report a taller ascent for QFontMetrics::height(). Without this
         * flag, QPainter::drawText(QRect, ...) clips glyphs to that exact
         * box, so an italic run's ascender got its top sheared off (a
         * real reported bug — badly enough that italic "void" was
         * visibly misreadable as "voia"). The outer paintEvent() clip
         * (docs/adr/0014) already keeps painting inside the viewport;
         * this per-run rect was never meant to also clip glyphs, only to
         * position/align them. */
        painter.drawText(QRect(x, y, runWidth, m_lineHeight), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextDontClip,
                          text);
        x += runWidth;
        runStart = i;
    }
}

/* Same run segmentation as drawLine, stopping at `column` instead of the
 * end of the line — so the caret lands exactly where drawLine actually
 * painted the character before it, even mid-run or mid-bold/italic-run.
 * See docs/adr/0013. */
int EditorViewport::xForColumn(int lineStart, int lineEnd, int column) const {
    int lineLen = lineEnd - lineStart;
    column = std::clamp(column, 0, lineLen);
    if (column == 0) {
        return 0;
    }

    QVector<AseHighlightCapture> captures = capturesForLine(lineStart, lineEnd);

    int x = 0;
    int runStart = 0;
    for (int i = 1; i <= column; ++i) {
        if (i < column && captures[i] == captures[runStart]) {
            continue;
        }
        int runLen = i - runStart;
        QString text = QString::fromUtf8(m_cache.constData() + lineStart + runStart, runLen);
        x += metricsForCapture(captures[runStart]).horizontalAdvance(text);
        runStart = i;
    }
    return x;
}

/* Inverse of xForColumn, for mouse hit-testing: which column's rendered
 * glyph localX falls nearest to. Walks the same per-run segmentation
 * xForColumn/drawLine use, measuring each run's *actual* advance
 * (QFontMetrics::horizontalAdvance) rather than assuming column *
 * m_charWidth. That fixed-pitch assumption is exactly the bug
 * docs/adr/0013 already fixed for the caret — it was still present here,
 * for the mouse: on a non-monospace-average line (bold/italic captures
 * measure differently than the base font — see fontForCapture), the
 * click/hover column drifted further from the actual character the
 * further right it landed on a line, a real reported bug. See
 * docs/adr/0039.
 *
 * Once inside the run that contains localX, steps by codepoint (not
 * byte) so a multi-byte UTF-8 character is never measured as a partial,
 * invalid byte sequence — isUtf8ContinuationByte is the same helper
 * offsetForPoint's own snap-forward and the cursor-movement code already
 * use elsewhere in this file. Rounds to the *nearest* codepoint boundary,
 * not floor — same reasoning as docs/adr/0028's click-precision fix. */
int EditorViewport::columnForX(int lineStart, int lineEnd, int localX) const {
    int lineLen = lineEnd - lineStart;
    if (lineLen <= 0 || localX <= 0) {
        return 0;
    }

    QVector<AseHighlightCapture> captures = capturesForLine(lineStart, lineEnd);

    int x = 0;
    int runStart = 0;
    for (int i = 1; i <= lineLen; ++i) {
        if (i < lineLen && captures[i] == captures[runStart]) {
            continue;
        }
        int runLen = i - runStart;
        const QFontMetrics &metrics = metricsForCapture(captures[runStart]);
        QString runText = QString::fromUtf8(m_cache.constData() + lineStart + runStart, runLen);
        int runWidth = metrics.horizontalAdvance(runText);

        if (x + runWidth >= localX) {
            int prevAdvance = 0;
            int prevBoundary = 0;
            int b = 1;
            while (b <= runLen) {
                while (b < runLen && isUtf8ContinuationByte(m_cache[lineStart + runStart + b])) {
                    b++;
                }
                QString prefix = QString::fromUtf8(m_cache.constData() + lineStart + runStart, b);
                int advance = metrics.horizontalAdvance(prefix);
                if (x + advance >= localX) {
                    int mid = x + (prevAdvance + advance) / 2;
                    return runStart + ((localX <= mid) ? prevBoundary : b);
                }
                prevAdvance = advance;
                prevBoundary = b;
                b++;
            }
            return runStart + runLen;
        }
        x += runWidth;
        runStart = i;
    }
    return lineLen;
}

/* Shared by the selection pass and the find/replace-match pass — one
 * rect per visual line in [firstLine, lastLine) that [start, end)
 * touches, using the same xForColumn measurement text/carets use. See
 * docs/adr/0019, docs/adr/0021. */
void EditorViewport::highlightRange(QPainter &painter, size_t start, size_t end, int firstLine, int lastLine,
                                     const QColor &color) const {
    if (start >= end) {
        return;
    }
    int startLine = lineForOffset(start);
    int endLine = lineForOffset(end);
    for (int line = std::max(firstLine, startLine); line <= std::min(lastLine - 1, endLine); ++line) {
        int lineStart = m_lineStarts[line];
        int lineEnd = (line + 1 < m_lineStarts.size()) ? m_lineStarts[line + 1] - 1 : static_cast<int>(m_cache.size());
        int rangeStartCol = (line == startLine) ? static_cast<int>(start) - lineStart : 0;
        int rangeEndCol = (line == endLine) ? static_cast<int>(end) - lineStart : lineEnd - lineStart;
        int x0 = xForColumn(lineStart, lineEnd, rangeStartCol);
        int x1 = xForColumn(lineStart, lineEnd, rangeEndCol);
        int y = (line - firstLine) * m_lineHeight;
        int rectWidth = x1 - x0;
        if (line < endLine) {
            rectWidth += m_charWidth / 2; /* hints the range's line break continues */
        }
        painter.fillRect(QRectF(x0, y, std::max(1, rectWidth), m_lineHeight), color);
    }
}

/* A thin straight underline across [start, end) — same per-line
 * splitting as highlightRange, sitting just under the text baseline
 * instead of filling the whole line height. Was a wavy zigzag
 * (docs/adr/0029); replaced with a plain line, dim by default and
 * brightening to full opacity — wiped in/out left-to-right across just
 * this diagnostic's own span, not the whole line — while the cursor
 * sits on that line. See docs/adr/0041. */
void EditorViewport::drawDiagnosticUnderline(QPainter &painter, size_t start, size_t end, int firstLine, int lastLine,
                                              const QColor &color) const {
    if (start >= end) {
        return;
    }
    int startLine = lineForOffset(start);
    int endLine = lineForOffset(end);
    painter.save();
    painter.setBrush(Qt::NoBrush);
    painter.setRenderHint(QPainter::Antialiasing, true);

    for (int line = std::max(firstLine, startLine); line <= std::min(lastLine - 1, endLine); ++line) {
        int lineStart = m_lineStarts[line];
        int lineEnd = (line + 1 < m_lineStarts.size()) ? m_lineStarts[line + 1] - 1 : static_cast<int>(m_cache.size());
        int rangeStartCol = (line == startLine) ? static_cast<int>(start) - lineStart : 0;
        int rangeEndCol = (line == endLine) ? static_cast<int>(end) - lineStart : lineEnd - lineStart;
        int x0 = xForColumn(lineStart, lineEnd, rangeStartCol);
        int x1 = xForColumn(lineStart, lineEnd, rangeEndCol);
        if (line < endLine) {
            x1 += m_charWidth / 2;
        }
        x1 = std::max(x0 + 1, x1);
        /* 1.0, not the old squiggle's 3.0 — shifted down slightly for
         * clearance from low-hanging descenders like an underscore. */
        double baseY = (line - firstLine) * m_lineHeight + m_lineHeight - 1.0;

        QColor dim = color;
        dim.setAlpha(kDiagnosticUnderlineDimAlpha);
        QPen pen(dim);
        pen.setWidthF(1.2);
        painter.setPen(pen);
        painter.drawLine(QPointF(x0, baseY), QPointF(x1, baseY));

        double reveal = diagnosticRevealForLine(line);
        if (reveal > 0.0) {
            QColor focused = color;
            focused.setAlpha(kDiagnosticUnderlineFocusAlpha);
            pen.setColor(focused);
            painter.setPen(pen);
            double revealX1 = x0 + (x1 - x0) * reveal;
            painter.drawLine(QPointF(x0, baseY), QPointF(revealX1, baseY));
        }
    }
    painter.restore();
}

/* See docs/adr/0042. The logo is the hand-drawn "ase" wordmark already
 * used for the app icon and About panel (docs/adr/0027, docs/adr/0034)
 * — it already spells out the name, so no redundant text title is
 * drawn under it here. Absolute widget coordinates (not the
 * gutter/scroll-translated space drawLine et al. use) since this is
 * centered on the viewport itself, independent of the (always-empty,
 * at scroll position zero) document underneath it. */
void EditorViewport::drawWelcomeOverlay(QPainter &painter) const {
    if (m_welcomeOverlayOpacity <= 0.0) {
        return;
    }

    painter.save();
    painter.setOpacity(m_welcomeOverlayOpacity);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    QPixmap logo(QStringLiteral(":/ase.png"));
    QPixmap scaledLogo;
    if (!logo.isNull()) {
        scaledLogo = logo.scaledToWidth(140, Qt::SmoothTransformation);
    }

    /* {description, key} — description reads left-to-right like a
     * legend ("what it does"), key answers "how" on the right, a small
     * bullet marks each row on the left. A flat centered line per
     * shortcut (the first version of this) read as visually
     * disorganized once there was more than one; this two-column,
     * dot-led layout is the aesthetic fix, still just the essentials,
     * not the Help panel's full reference. */
    struct WelcomeInstruction {
        QString description;
        QString key;
    };
    static const QVector<WelcomeInstruction> kInstructions = {
        {QStringLiteral("Open a file"), QStringLiteral("Ctrl+O")},
        {QStringLiteral("Save"), QStringLiteral("Ctrl+S")},
        {QStringLiteral("Keyboard shortcuts"), QStringLiteral("Ctrl+/")},
        {QStringLiteral("About"), QStringLiteral("Ctrl+I")},
    };
    QFontMetrics metrics(m_font);
    int lineSpacing = metrics.height() + 4;
    constexpr int kLogoTextGap = 22;
    constexpr double kDotSize = 5.0;
    constexpr int kDotTextGap = 12;
    constexpr int kColumnGap = 32;

    int maxDescWidth = 0;
    int maxKeyWidth = 0;
    for (const WelcomeInstruction &instr : kInstructions) {
        maxDescWidth = std::max(maxDescWidth, metrics.horizontalAdvance(instr.description));
        maxKeyWidth = std::max(maxKeyWidth, metrics.horizontalAdvance(instr.key));
    }
    int rowWidth = static_cast<int>(kDotSize) + kDotTextGap + maxDescWidth + kColumnGap + maxKeyWidth;

    int totalHeight = scaledLogo.height() + (scaledLogo.isNull() ? 0 : kLogoTextGap) +
                       kInstructions.size() * lineSpacing;
    int top = (height() - totalHeight) / 2;

    if (!scaledLogo.isNull()) {
        painter.drawPixmap((width() - scaledLogo.width()) / 2, top, scaledLogo);
    }

    QColor textColor = m_textColor;
    textColor.setAlpha(160);
    painter.setFont(m_font);
    int rowLeft = (width() - rowWidth) / 2;
    int descLeft = rowLeft + static_cast<int>(kDotSize) + kDotTextGap;
    int keyLeft = rowLeft + rowWidth - maxKeyWidth;
    int textTop = top + scaledLogo.height() + (scaledLogo.isNull() ? 0 : kLogoTextGap);
    for (const WelcomeInstruction &instr : kInstructions) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(textColor);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.drawEllipse(QRectF(rowLeft, textTop + (lineSpacing - kDotSize) / 2.0, kDotSize, kDotSize));

        painter.setPen(textColor);
        painter.drawText(QRect(descLeft, textTop, maxDescWidth, lineSpacing), Qt::AlignLeft | Qt::AlignVCenter,
                          instr.description);
        painter.drawText(QRect(keyLeft, textTop, maxKeyWidth, lineSpacing), Qt::AlignRight | Qt::AlignVCenter,
                          instr.key);
        textTop += lineSpacing;
    }

    painter.restore();
}

int EditorViewport::gutterWidth() const {
    if (m_lineNumberMode == QLatin1String("off")) {
        return 0;
    }
    /* Measured from the actual widest label, not digit-count * a fixed
     * per-digit width — same reasoning as the caret-drift fix, applied
     * to new code. See docs/adr/0014, decision 4. */
    QString widest = QString::number(std::max(1, static_cast<int>(m_lineStarts.size())));
    int textWidth = QFontMetrics(m_font).horizontalAdvance(widest);
    return textWidth + kGutterPadding * 2;
}

QString EditorViewport::gutterLabelForLine(int line, int cursorLine) const {
    if (m_lineNumberMode == QLatin1String("relative") && line != cursorLine) {
        return QString::number(std::abs(line - cursorLine));
    }
    return QString::number(line + 1); /* 1-based display */
}

QFont EditorViewport::fontForCapture(AseHighlightCapture capture) const {
    QFont font = m_font;
    if (capture == ASE_HL_KEYWORD) {
        font.setBold(true);
    } else if (capture == ASE_HL_TYPE) {
        font.setItalic(true);
    }
    return font;
}

const QFontMetrics &EditorViewport::metricsForCapture(AseHighlightCapture capture) const {
    if (capture == ASE_HL_KEYWORD) {
        return m_boldMetrics;
    }
    if (capture == ASE_HL_TYPE) {
        return m_italicMetrics;
    }
    return m_metrics;
}

QColor EditorViewport::colorForCapture(AseHighlightCapture capture) const {
    QColor color = m_textColor;
    switch (capture) {
    case ASE_HL_STRING:
    case ASE_HL_NUMBER:
        color.setAlpha(200);
        break;
    case ASE_HL_COMMENT:
        /* 145/255 (~57%), not the original 115/255 (~45%) — that measured
         * 3.64:1 against the background, below WCAG AA's 4.5:1 for normal
         * text. See docs/adr/0012, decision 4. */
        color.setAlpha(145);
        break;
    case ASE_HL_KEYWORD:
    case ASE_HL_TYPE:
    case ASE_HL_NONE:
    default:
        break;
    }
    return color;
}

void EditorViewport::keyPressEvent(QKeyEvent *event) {
    /* A modal panel already has keyboard focus in the normal case, so
     * this doesn't usually even get reached while one is open — this
     * is the defense-in-depth half of docs/adr/0044's isolation fix,
     * guaranteeing no keystroke edits the document underneath
     * regardless of any focus-routing edge case. */
    if (isModalPanelOpen()) {
        QWidget::keyPressEvent(event);
        return;
    }

    resetCaretBlink();
    dismissHover();

    /* Completion popup interception — see docs/adr/0030. Takes priority
     * over the Up/Down/Escape handling below and over Key_Return's
     * normal "insert a newline" case further down; any other key
     * (including plain typing) falls through to the normal handling,
     * which itself retriggers a fresh completion request via
     * refreshCache(). */
    if (m_completionPopup != nullptr && m_completionPopup->isShowingPopup()) {
        switch (event->key()) {
        case Qt::Key_Up:
            m_completionPopup->moveSelection(-1);
            return;
        case Qt::Key_Down:
            m_completionPopup->moveSelection(1);
            return;
        case Qt::Key_Escape:
            dismissCompletion();
            return;
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Tab:
            acceptCompletion();
            return;
        default:
            break;
        }
    }

    bool extend = event->modifiers() & Qt::ShiftModifier;

    if (event->key() == Qt::Key_Up) {
        moveCursorVertically(-1, extend);
        ensureCursorVisible();
        update();
        return;
    }
    if (event->key() == Qt::Key_Down) {
        moveCursorVertically(1, extend);
        ensureCursorVisible();
        update();
        return;
    }
    if (event->key() == Qt::Key_Escape) {
        if (vimModeActive() && m_vimMode == VimMode::Insert) {
            /* Leaving Insert steps back one column, same as real vim —
             * only when not already at column 0, so Escape on an empty
             * or just-Home'd line doesn't try to move left of it. */
            if (m_cursors[0] > static_cast<size_t>(m_lineStarts[lineForOffset(m_cursors[0])])) {
                moveCursorLeftAt(0, false);
            }
            m_vimMode = VimMode::Normal;
            resetVimPendingState();
        } else if (vimModeActive()) {
            /* From Normal or Visual: drop any pending operator/count and
             * any Visual-mode selection, stay/return to Normal. */
            resetVimPendingState();
            collapseToOneCursor();
            m_vimMode = VimMode::Normal;
        } else {
            collapseToOneCursor();
        }
        ensureCursorVisible();
        update();
        return;
    }
    m_desiredColumn = -1;

    if (vimModeActive() && m_vimMode != VimMode::Insert) {
        if (handleVimNormalOrVisualKey(event)) {
            return;
        }
        /* Falls through only for keys Vim doesn't claim (arrows, Home/
         * End, every Ctrl/Alt/Meta combo) — the switch/Ctrl-chain below
         * handles those exactly as it does with Vim mode off. */
    }
    /* Set only for plain character insertion — see
     * snapAnimationToTarget's doc comment (docs/adr/0017): typing
     * always renders instantly, even with animations on, because
     * gliding can't keep pace with fast repeated small jumps and the
     * result reads as lag, not smoothness. Enter and Backspace/Delete
     * are deliberately *not* in that bucket (see their own cases
     * below, and docs/adr/0028) — each is a single, discrete cursor
     * jump rather than a rapid sequence, so it doesn't have that
     * problem and gets to glide like navigation does. */
    bool isEdit = false;

    switch (event->key()) {
    case Qt::Key_Left:
        moveCursorLeft(extend);
        break;
    case Qt::Key_Right:
        moveCursorRight(extend);
        break;
    case Qt::Key_Home:
        moveCursorHome(extend);
        break;
    case Qt::Key_End:
        moveCursorEnd(extend);
        break;
    case Qt::Key_Backspace:
        /* Not isEdit = true, matching Enter — see docs/adr/0028 and
         * the comment on the Return/Enter case below. Deleting a
         * newline (merging two lines) is the same kind of discrete
         * jump Enter makes, worth gliding; deleting an ordinary
         * character is a one-column move, animated or not. */
        deleteBackward();
        break;
    case Qt::Key_Delete:
        /* Real vim gives Delete no default Normal-mode meaning either —
         * see docs/adr/0046. Reachable here at all only because Delete
         * isn't part of Vim's own recognized key set (handleVimNormal-
         * OrVisualKey returns false for it), by design. */
        if (vimModeActive() && m_vimMode != VimMode::Insert) {
            break;
        }
        deleteForward();
        break;
    case Qt::Key_Tab:
        /* Qt::Key_Tab's event->text() is "\t", a control character —
         * QChar::isPrint() is false for it, so it fell through to the
         * default branch below and out to QWidget::keyPressEvent
         * (Qt's default focus-traversal handling), meaning it did
         * nothing at all: a real, reported gap, not a deliberate
         * omission. Inserts 4 spaces, not a literal tab byte:
         * drawLine/xForColumn measure each run with plain
         * QFontMetrics::horizontalAdvance (no QTextLayout, no tab-stop
         * expansion), so a raw '\t' would measure at ~0 width and
         * render as an invisible non-indent — a soft tab renders
         * correctly with the exact same per-glyph measurement every
         * other character already uses, and is simplicity keeping with
         * the byte-level column model (docs/adr/0012) rather than
         * adding tab-stop-aware rendering for one key. Treated as a
         * plain character insertion (isEdit = true, instant, not
         * glided). Completion-popup Tab-to-accept is intercepted
         * earlier in this function and never reaches here. See
         * docs/adr/0031. Real vim gives Tab no default Normal-mode
         * meaning either — see docs/adr/0046 — so this is skipped
         * while Vim mode is on and not in Insert. */
        if (vimModeActive() && m_vimMode != VimMode::Insert) {
            break;
        }
        insertText(QByteArrayLiteral("    "));
        isEdit = true;
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        /* Deliberately *not* isEdit = true — see docs/adr/0027 and
         * docs/adr/0028. A newline is a single, discrete jump to a new
         * line, closer in feel to navigation than to character-by-
         * character typing, and it's the first edit the user asked to
         * see glide (Backspace/Delete followed once deletion was
         * asked for too). Regular character insertion stays instant —
         * ADR 0017's original reasoning (gliding can't keep pace with
         * fast repeated small jumps) still holds for that case.
         * Real vim gives Enter no default Normal-mode meaning either —
         * see docs/adr/0046 — so this is skipped while Vim mode is on
         * and not in Insert. */
        if (vimModeActive() && m_vimMode != VimMode::Insert) {
            break;
        }
        insertText(QByteArrayLiteral("\n"));
        break;
    default:
        if (event->modifiers() & Qt::ControlModifier) {
            if (event->key() == Qt::Key_S) {
                /* Ctrl+Shift+S always opens Save-As, even with a path
                 * already set — "save as" means "let me pick a
                 * different one," not "save.". Ctrl+S with no path set
                 * falls through to save()'s own Save-As fallback. */
                if (event->modifiers() & Qt::ShiftModifier) {
                    if (m_fileBrowser != nullptr) {
                        m_fileBrowser->openFor(FileBrowserPanel::Mode::SaveAs);
                    }
                } else {
                    save();
                }
                return;
            }
            if (event->key() == Qt::Key_O) {
                /* Ctrl+Shift+O toggles the output panel directly,
                 * without going through :output — Ctrl+O (no Shift)
                 * keeps its existing meaning, Open. */
                if (event->modifiers() & Qt::ShiftModifier) {
                    toggleOutputPanel();
                } else if (m_fileBrowser != nullptr) {
                    m_fileBrowser->openFor(FileBrowserPanel::Mode::Open);
                }
                return;
            }
            if (event->key() == Qt::Key_Semicolon) {
                /* Command-line trigger — Ctrl+; here, not a bare `:`
                 * (that's the ex-command-line convention Vim mode will
                 * use later; in normal mode a bare `:` has to stay a
                 * literal, typeable character). See docs/adr/0025. */
                if (m_commandLine != nullptr) {
                    m_commandLine->openCommandLine();
                }
                return;
            }
            if (event->key() == Qt::Key_B) {
                compile();
                return;
            }
            if (event->key() == Qt::Key_Q) {
                window()->close();
                return;
            }
            if (event->key() == Qt::Key_Slash) {
                if (m_helpPanel != nullptr) {
                    m_helpPanel->openHelp();
                }
                return;
            }
            if (event->key() == Qt::Key_I) {
                if (m_aboutPanel != nullptr) {
                    m_aboutPanel->openAbout();
                }
                return;
            }
            if (event->key() == Qt::Key_D) {
                addCursorAtNextOccurrence();
                return;
            }
            if (event->key() == Qt::Key_A) {
                selectAll();
                return;
            }
            if (event->key() == Qt::Key_C) {
                copySelection();
                return;
            }
            if (event->key() == Qt::Key_X) {
                cutSelection();
                return;
            }
            if (event->key() == Qt::Key_V) {
                pasteClipboard();
                return;
            }
            if (event->key() == Qt::Key_F) {
                if (m_findBar != nullptr) {
                    m_findBar->openFor(FindBar::Mode::Find);
                }
                return;
            }
            if (event->key() == Qt::Key_H) {
                if (m_findBar != nullptr) {
                    m_findBar->openFor(FindBar::Mode::Replace);
                }
                return;
            }
            if (event->key() == Qt::Key_Z) {
                if (event->modifiers() & Qt::ShiftModifier) {
                    redo();
                } else {
                    undo();
                }
                return;
            }
            if (event->key() == Qt::Key_R && vimModeActive()) {
                /* Vim's own redo binding, additive to the existing
                 * Ctrl+Shift+Z above — gated on vimModeActive() (not on
                 * m_vimMode) so it works from Insert too, matching how
                 * Ctrl+Z/Ctrl+Shift+Z are already mode-independent, and
                 * so non-Vim users see no new shortcut. */
                redo();
                return;
            }
            QWidget::keyPressEvent(event);
            return;
        }

        {
            const QString text = event->text();
            if (text.isEmpty() || !text.at(0).isPrint()) {
                QWidget::keyPressEvent(event);
                return;
            }
            /* Defense-in-depth, not the primary enforcement point: the
             * gate above (m_desiredColumn = -1's neighbor) already
             * claims every printable key while Vim mode is on and not
             * in Insert, so this should be unreachable in that state —
             * see docs/adr/0046. */
            if (vimModeActive() && m_vimMode != VimMode::Insert) {
                return;
            }
            insertText(text.toUtf8());
            isEdit = true;
        }
    }

    ensureCursorVisible();
    if (isEdit) {
        snapAnimationToTarget();
    }
    update();
}

void EditorViewport::wheelEvent(QWheelEvent *event) {
    /* See docs/adr/0044 — a modal panel doesn't cover the whole
     * viewport, so without this the document visible around/behind it
     * could still be scrolled while it's open. */
    if (isModalPanelOpen()) {
        QWidget::wheelEvent(event);
        return;
    }

    /* Scrolling moves the caret's on-screen position without moving the
     * cursor itself — both popups anchor to a screen point computed at
     * request time, so neither would track the new scroll offset. See
     * docs/adr/0030. */
    dismissCompletion();
    dismissHover();
    int lines = event->angleDelta().y() / 40;
    int maxScroll = std::max(0, static_cast<int>(m_lineStarts.size()) - 1);
    m_scrollLine = std::clamp(m_scrollLine - lines, 0, maxScroll);
    update();
}

void EditorViewport::mousePressEvent(QMouseEvent *event) {
    /* Blocks the click outright rather than also treating it as
     * "clicked away, so close the panel" — a non-interactive widget
     * inside an open panel (a QLabel, empty layout space) ignores a
     * press and Qt bubbles it up to here exactly the same way a
     * genuine outside click would arrive, so the two aren't reliably
     * distinguishable at this level; a real double-click inside a
     * panel bubbles the same way too. Rather than chase every such
     * case, closing is Escape-only (each panel's own eventFilter
     * handles that directly) — a click while a panel is open just does
     * nothing. See docs/adr/0044. */
    if (isModalPanelOpen()) {
        return;
    }

    dismissCompletion();
    dismissHover();

    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    size_t offset = offsetForPoint(event->position().toPoint());

    if (event->modifiers() & Qt::ShiftModifier) {
        /* Extends the primary (last) cursor's selection to the click
         * point instead of clearing — its anchor is left untouched, so a
         * fresh Shift+click after a plain click anchors at the old
         * cursor position. Multi-cursor + Shift+click isn't a scoped
         * combination (drag-selection is single-cursor-only too, see
         * mouseMoveEvent) — only the last cursor is affected. */
        if (!m_cursors.isEmpty()) {
            m_cursors.last() = offset;
        }
    } else if (event->modifiers() & Qt::AltModifier) {
        m_cursors.push_back(offset);
        m_selectionAnchors.push_back(offset);
        normalizeCursors();
    } else {
        m_cursors.clear();
        m_selectionAnchors.clear();
        m_cursors.push_back(offset);
        m_selectionAnchors.push_back(offset);
    }

    m_desiredColumn = -1;
    resetCaretBlink();
    ensureCursorVisible();
    update();
}

/* While the left button is held, Qt keeps delivering move events to this
 * widget (implicit press-grab) regardless of setMouseTracking — no extra
 * grab needed. Only extends the single-cursor case: a plain press already
 * collapsed to one cursor, so a drag starting from a multi-cursor state
 * can't happen. See docs/adr/0019. */
void EditorViewport::mouseMoveEvent(QMouseEvent *event) {
    /* See docs/adr/0044 — skips both the passive-hover-popup branch and
     * drag-select below while a modal panel is open, same reasoning as
     * the wheelEvent guard above. */
    if (isModalPanelOpen()) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    if (!(event->buttons() & Qt::LeftButton)) {
        /* No button held — passive movement, i.e. hover tracking (see
         * docs/adr/0030), not a drag. */
        scheduleHoverRequest(event->position().toPoint());
        QWidget::mouseMoveEvent(event);
        return;
    }
    if (m_cursors.size() != 1) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    dismissHover();
    m_cursors[0] = offsetForPoint(event->position().toPoint());
    resetCaretBlink();
    ensureCursorVisible();
    update();
}

void EditorViewport::leaveEvent(QEvent *event) {
    dismissHover();
    QWidget::leaveEvent(event);
}

void EditorViewport::focusOutEvent(QFocusEvent *event) {
    dismissCompletion();
    dismissHover();
    QWidget::focusOutEvent(event);
}

void EditorViewport::normalizeCursors() {
    QVector<size_t> anchors = m_selectionAnchors;
    QVector<int> order(m_cursors.size());
    for (int i = 0; i < order.size(); ++i) {
        order[i] = i;
    }
    std::sort(order.begin(), order.end(),
              [this](int a, int b) { return m_cursors[a] < m_cursors[b]; });

    QVector<size_t> sortedCursors;
    QVector<size_t> sortedAnchors;
    sortedCursors.reserve(order.size());
    sortedAnchors.reserve(order.size());
    for (int idx : order) {
        if (!sortedCursors.isEmpty() && sortedCursors.last() == m_cursors[idx]) {
            continue; /* de-dupe by cursor position; keep the first anchor seen */
        }
        sortedCursors.push_back(m_cursors[idx]);
        sortedAnchors.push_back(anchors[idx]);
    }

    m_cursors = std::move(sortedCursors);
    m_selectionAnchors = std::move(sortedAnchors);
    if (m_cursors.isEmpty()) {
        m_cursors.push_back(0);
        m_selectionAnchors.push_back(0);
    }
}

void EditorViewport::collapseToOneCursor() {
    if (m_cursors.size() <= 1) {
        m_selectionAnchors[0] = m_cursors[0]; /* Escape also clears an active selection */
        return;
    }
    size_t keep = m_cursors.last();
    m_cursors.clear();
    m_selectionAnchors.clear();
    m_cursors.push_back(keep);
    m_selectionAnchors.push_back(keep);
}

/* "Select next occurrence" (Ctrl+D, Sublime/VS Code convention) without a
 * selection-range model: adds a point cursor at the end of the next
 * whole-word match after the word at the last cursor. Doesn't wrap. */
void EditorViewport::addCursorAtNextOccurrence() {
    if (m_cursors.isEmpty()) {
        return;
    }
    size_t anchor = m_cursors.last();
    int len = m_cache.size();

    int wordStart = static_cast<int>(anchor);
    while (wordStart > 0 && isWordChar(m_cache[wordStart - 1])) {
        wordStart--;
    }
    int wordEnd = static_cast<int>(anchor);
    while (wordEnd < len && isWordChar(m_cache[wordEnd])) {
        wordEnd++;
    }
    if (wordStart == wordEnd) {
        return; /* anchor isn't touching a word */
    }

    QByteArray word = m_cache.mid(wordStart, wordEnd - wordStart);
    int wordLen = word.size();

    for (int searchStart = wordEnd; searchStart + wordLen <= len; ++searchStart) {
        if (m_cache.mid(searchStart, wordLen) != word) {
            continue;
        }
        bool boundaryBefore = (searchStart == 0) || !isWordChar(m_cache[searchStart - 1]);
        bool boundaryAfter = (searchStart + wordLen == len) || !isWordChar(m_cache[searchStart + wordLen]);
        if (boundaryBefore && boundaryAfter) {
            size_t newCursor = static_cast<size_t>(searchStart + wordLen);
            m_cursors.push_back(newCursor);
            m_selectionAnchors.push_back(newCursor);
            normalizeCursors();
            ensureCursorVisible();
            update();
            return;
        }
    }
    /* no further occurrence forward — no-op, see docs/adr/0012 */
}

/* Collapses to one cursor with a selection spanning the whole buffer —
 * anchor at the start, head at the end, so it composes with everything
 * else that already treats "a selection" as just m_selectionAnchors[i]
 * != m_cursors[i] (copy, delete, replace-over-selection, ...). See
 * docs/adr/0028. */
void EditorViewport::selectAll() {
    m_cursors = {static_cast<size_t>(m_cache.size())};
    m_selectionAnchors = {0};
    m_desiredColumn = -1;
    resetCaretBlink();
    ensureCursorVisible();
    update();
}

bool EditorViewport::hasSelectionAt(int i) const {
    return m_selectionAnchors[i] != m_cursors[i];
}

size_t EditorViewport::selectionMinAt(int i) const {
    return std::min(m_cursors[i], m_selectionAnchors[i]);
}

size_t EditorViewport::selectionMaxAt(int i) const {
    return std::max(m_cursors[i], m_selectionAnchors[i]);
}

/* Reads the about-to-be-deleted range out of m_cache before deleting, same
 * reasoning as deleteBackwardAt/deleteForwardAt below (see docs/adr/0018)
 * — safe under the batch's highest-offset-first processing order. */
void EditorViewport::deleteSelectionAt(int i) {
    size_t start = selectionMinAt(i);
    size_t end = selectionMaxAt(i);
    QByteArray removed = m_cache.mid(static_cast<int>(start), static_cast<int>(end - start));
    if (ase_buffer_delete(m_buffer, start, end - start)) {
        ase_undo_record_delete(m_undo, start, removed.constData(), static_cast<size_t>(removed.size()));
    }
    m_cursors[i] = start;
    m_selectionAnchors[i] = start;
}

/* Reads straight out of m_cache rather than ase_buffer_get_text — it's
 * already a full, current mirror of the buffer (ADR 0006). Multiple
 * selections join with '\n', the standard multi-cursor copy convention.
 * See docs/adr/0020. */
bool EditorViewport::copySelection() {
    QStringList parts;
    for (int i = 0; i < m_cursors.size(); ++i) {
        if (!hasSelectionAt(i)) {
            continue;
        }
        size_t start = selectionMinAt(i);
        size_t end = selectionMaxAt(i);
        parts.push_back(
            QString::fromUtf8(m_cache.constData() + static_cast<int>(start), static_cast<int>(end - start)));
    }
    if (parts.isEmpty()) {
        return false;
    }
    QGuiApplication::clipboard()->setText(parts.join(QLatin1Char('\n')));
    return true;
}

/* Copy, then delete every selection as one undo group — reuses the
 * selection-delete path Phase 11 added. A no-op (clipboard untouched)
 * when nothing is selected anywhere. */
void EditorViewport::cutSelection() {
    if (!copySelection()) {
        return;
    }
    ase_undo_begin_group(m_undo, m_cursors.constData(), static_cast<size_t>(m_cursors.size()));
    for (int i = m_cursors.size() - 1; i >= 0; --i) {
        if (hasSelectionAt(i)) {
            deleteSelectionAt(i);
        }
    }
    normalizeCursors();
    ase_undo_end_group(m_undo, m_cursors.constData(), static_cast<size_t>(m_cursors.size()));
    m_dirty = true;
    refreshCache();
    ensureCursorVisible();
    snapAnimationToTarget();
    update();
}

/* Inserts the same clipboard text at every cursor (reusing insertText's
 * existing multi-cursor broadcast and selection-replace semantics) rather
 * than distributing clipboard lines one-per-cursor — a real feature some
 * editors have, but not worth the added complexity for v1. */
void EditorViewport::pasteClipboard() {
    QString text = QGuiApplication::clipboard()->text();
    if (text.isEmpty()) {
        return;
    }
    insertText(text.toUtf8());
    ensureCursorVisible();
    snapAnimationToTarget();
    update();
}

/* ------------------------------------------------------------- Vim mode */
/* Phase 1 — see docs/adr/0046. Everything below operates on m_cursors[0]
 * only; handleVimNormalOrVisualKey() collapses to one cursor the moment
 * any Vim key is pressed. */

void EditorViewport::resetVimPendingState() {
    m_vimCount1 = 0;
    m_vimCount2 = 0;
    m_vimPendingOperator = '\0';
    m_vimPendingG = false;
}

/* '\n' counts as Blank, not its own class — the key trick that makes
 * w/b/e cross line boundaries with zero special-casing, matching real
 * vim's "a blank line acts like whitespace" model. */
EditorViewport::VimCharClass EditorViewport::vimClassifyAt(size_t pos) const {
    char c = m_cache[static_cast<int>(pos)];
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        return VimCharClass::Blank;
    }
    if (isWordChar(c)) {
        return VimCharClass::Word;
    }
    return VimCharClass::Punct;
}

size_t EditorViewport::vimNextCharBoundary(size_t pos) const {
    size_t len = static_cast<size_t>(m_cache.size());
    if (pos >= len) {
        return len;
    }
    pos++;
    while (pos < len && isUtf8ContinuationByte(m_cache[static_cast<int>(pos)])) {
        pos++;
    }
    return pos;
}

size_t EditorViewport::vimPrevCharBoundary(size_t pos) const {
    if (pos == 0) {
        return 0;
    }
    pos--;
    while (pos > 0 && isUtf8ContinuationByte(m_cache[static_cast<int>(pos)])) {
        pos--;
    }
    return pos;
}

size_t EditorViewport::vimWordForward(size_t pos) const {
    size_t len = static_cast<size_t>(m_cache.size());
    if (pos >= len) {
        return len;
    }
    VimCharClass start = vimClassifyAt(pos);
    if (start != VimCharClass::Blank) {
        while (pos < len && vimClassifyAt(pos) == start) {
            pos = vimNextCharBoundary(pos);
        }
    }
    while (pos < len && vimClassifyAt(pos) == VimCharClass::Blank) {
        pos = vimNextCharBoundary(pos);
    }
    return pos;
}

size_t EditorViewport::vimWordEnd(size_t pos) const {
    size_t len = static_cast<size_t>(m_cache.size());
    if (pos >= len) {
        return pos;
    }
    pos = vimNextCharBoundary(pos);
    while (pos < len && vimClassifyAt(pos) == VimCharClass::Blank) {
        pos = vimNextCharBoundary(pos);
    }
    if (pos >= len) {
        return len;
    }
    VimCharClass cls = vimClassifyAt(pos);
    size_t next = vimNextCharBoundary(pos);
    while (next < len && vimClassifyAt(next) == cls) {
        pos = next;
        next = vimNextCharBoundary(next);
    }
    return pos;
}

size_t EditorViewport::vimWordBackward(size_t pos) const {
    if (pos == 0) {
        return 0;
    }
    pos = vimPrevCharBoundary(pos);
    while (pos > 0 && vimClassifyAt(pos) == VimCharClass::Blank) {
        pos = vimPrevCharBoundary(pos);
    }
    if (vimClassifyAt(pos) == VimCharClass::Blank) {
        return 0;
    }
    VimCharClass cls = vimClassifyAt(pos);
    while (pos > 0) {
        size_t prev = vimPrevCharBoundary(pos);
        if (vimClassifyAt(prev) != cls) {
            break;
        }
        pos = prev;
    }
    return pos;
}

size_t EditorViewport::vimFirstNonBlank(int line) const {
    line = std::clamp(line, 0, static_cast<int>(m_lineStarts.size()) - 1);
    int start = m_lineStarts[line];
    int end = (line + 1 < m_lineStarts.size()) ? m_lineStarts[line + 1] - 1 : static_cast<int>(m_cache.size());
    int pos = start;
    while (pos < end && (m_cache[pos] == ' ' || m_cache[pos] == '\t')) {
        pos++;
    }
    return static_cast<size_t>(pos);
}

void EditorViewport::vimGotoLine(int line) {
    line = std::clamp(line, 0, static_cast<int>(m_lineStarts.size()) - 1);
    if (m_vimPendingOperator != '\0') {
        int beforeLine = lineForOffset(m_cursors[0]);
        int startLine = std::min(beforeLine, line);
        int lineCount = std::abs(line - beforeLine) + 1;
        vimApplyPendingOperatorLinewise(startLine, lineCount);
        return;
    }
    size_t target = vimFirstNonBlank(line);
    m_cursors[0] = target;
    if (m_vimMode != VimMode::Visual) {
        m_selectionAnchors[0] = target;
    }
}

void EditorViewport::vimExecuteMotion(char m, int count) {
    bool visual = (m_vimMode == VimMode::Visual);
    size_t before = m_cursors[0];

    if (m_vimPendingOperator == '\0') {
        for (int n = 0; n < count; ++n) {
            switch (m) {
            case 'h':
                moveCursorLeftAt(0, visual);
                break;
            case 'l':
                moveCursorRightAt(0, visual);
                break;
            case 'j':
                moveCursorVerticallyAt(0, 1, visual);
                break;
            case 'k':
                moveCursorVerticallyAt(0, -1, visual);
                break;
            case '0':
                moveCursorHomeAt(0, visual);
                return;
            case '^': {
                size_t target = vimFirstNonBlank(lineForOffset(m_cursors[0]));
                m_cursors[0] = target;
                if (!visual) {
                    m_selectionAnchors[0] = target;
                }
                return;
            }
            case '$':
                moveCursorEndAt(0, visual);
                return;
            case 'w': {
                size_t target = vimWordForward(m_cursors[0]);
                m_cursors[0] = target;
                if (!visual) {
                    m_selectionAnchors[0] = target;
                }
                break;
            }
            case 'b': {
                size_t target = vimWordBackward(m_cursors[0]);
                m_cursors[0] = target;
                if (!visual) {
                    m_selectionAnchors[0] = target;
                }
                break;
            }
            case 'e': {
                size_t target = vimWordEnd(m_cursors[0]);
                m_cursors[0] = target;
                if (!visual) {
                    m_selectionAnchors[0] = target;
                }
                break;
            }
            default:
                return;
            }
        }
        return;
    }

    /* Operator pending — j/k span whole lines; everything else is an
     * exact byte range from `before` to wherever `count` applications
     * of the motion land. */
    if (m == 'j' || m == 'k') {
        int beforeLine = lineForOffset(before);
        int deltaLines = (m == 'j') ? count : -count;
        int targetLine = std::clamp(beforeLine + deltaLines, 0, static_cast<int>(m_lineStarts.size()) - 1);
        int startLine = std::min(beforeLine, targetLine);
        int lineCount = std::abs(targetLine - beforeLine) + 1;
        vimApplyPendingOperatorLinewise(startLine, lineCount);
        return;
    }

    size_t after = before;
    if (m == '0') {
        after = static_cast<size_t>(m_lineStarts[lineForOffset(before)]);
    } else if (m == '^') {
        after = vimFirstNonBlank(lineForOffset(before));
    } else if (m == '$') {
        int line = lineForOffset(before);
        after = (line + 1 < m_lineStarts.size()) ? static_cast<size_t>(m_lineStarts[line + 1] - 1)
                                                  : static_cast<size_t>(m_cache.size());
    } else {
        for (int n = 0; n < count; ++n) {
            switch (m) {
            case 'h':
                after = vimPrevCharBoundary(after);
                break;
            case 'l':
                after = vimNextCharBoundary(after);
                break;
            case 'w':
                after = vimWordForward(after);
                break;
            case 'b':
                after = vimWordBackward(after);
                break;
            case 'e':
                /* Inclusive of the char the motion itself lands on —
                 * "d/y/c to the end of this word" removes that last
                 * character too, unlike a plain 'e' cursor move. */
                after = vimNextCharBoundary(vimWordEnd(after));
                break;
            default:
                break;
            }
        }
    }
    size_t start = std::min(before, after);
    size_t end = std::max(before, after);
    vimApplyPendingOperatorCharwise(start, end);
}

void EditorViewport::vimApplyPendingOperatorCharwise(size_t start, size_t end) {
    char op = m_vimPendingOperator;
    resetVimPendingState();
    if (start >= end) {
        return;
    }
    switch (op) {
    case 'd':
        vimDeleteRange(start, end);
        break;
    case 'y':
        vimYankRange(start, end, false);
        break;
    case 'c':
        vimChangeRange(start, end);
        break;
    default:
        break;
    }
}

void EditorViewport::vimApplyPendingOperatorLinewise(int startLine, int lineCount) {
    char op = m_vimPendingOperator;
    resetVimPendingState();
    switch (op) {
    case 'd':
        vimDeleteLines(startLine, lineCount);
        break;
    case 'y':
        vimYankLines(startLine, lineCount);
        break;
    case 'c':
        /* Real vim leaves one blank line and enters insert there; v1
         * simplification (documented, docs/adr/0046) — just delete the
         * lines and enter insert at the resulting cursor position. */
        vimDeleteLines(startLine, lineCount);
        m_vimMode = VimMode::Insert;
        break;
    default:
        break;
    }
}

void EditorViewport::vimDeleteRange(size_t start, size_t end) {
    QByteArray removed = m_cache.mid(static_cast<int>(start), static_cast<int>(end - start));
    ase_undo_begin_group(m_undo, m_cursors.constData(), static_cast<size_t>(m_cursors.size()));
    if (ase_buffer_delete(m_buffer, start, end - start)) {
        ase_undo_record_delete(m_undo, start, removed.constData(), static_cast<size_t>(removed.size()));
    }
    m_cursors[0] = start;
    m_selectionAnchors[0] = start;
    ase_undo_end_group(m_undo, m_cursors.constData(), static_cast<size_t>(m_cursors.size()));
    m_dirty = true;
    refreshCache();
    ensureCursorVisible();
    snapAnimationToTarget();
    update();
}

void EditorViewport::vimYankRange(size_t start, size_t end, bool linewise) {
    QByteArray text = m_cache.mid(static_cast<int>(start), static_cast<int>(end - start));
    QGuiApplication::clipboard()->setText(QString::fromUtf8(text));
    m_vimLastYankWasLinewise = linewise;
    m_cursors[0] = start;
    m_selectionAnchors[0] = start;
    ensureCursorVisible();
    update();
}

void EditorViewport::vimChangeRange(size_t start, size_t end) {
    vimDeleteRange(start, end);
    m_vimMode = VimMode::Insert;
}

void EditorViewport::vimDeleteLines(int startLine, int count) {
    startLine = std::clamp(startLine, 0, static_cast<int>(m_lineStarts.size()) - 1);
    int endLine = std::clamp(startLine + count - 1, startLine, static_cast<int>(m_lineStarts.size()) - 1);
    size_t start = static_cast<size_t>(m_lineStarts[startLine]);
    /* Consume through the start of the line *after* endLine so the
     * trailing newline goes with it too (leaves no blank line behind);
     * on the buffer's last line, there's no following '\n' to eat. */
    size_t end = (endLine + 1 < m_lineStarts.size()) ? static_cast<size_t>(m_lineStarts[endLine + 1])
                                                      : static_cast<size_t>(m_cache.size());
    QByteArray removed = m_cache.mid(static_cast<int>(start), static_cast<int>(end - start));
    ase_undo_begin_group(m_undo, m_cursors.constData(), static_cast<size_t>(m_cursors.size()));
    if (ase_buffer_delete(m_buffer, start, end - start)) {
        ase_undo_record_delete(m_undo, start, removed.constData(), static_cast<size_t>(removed.size()));
    }
    ase_undo_end_group(m_undo, m_cursors.constData(), static_cast<size_t>(m_cursors.size()));
    m_dirty = true;
    refreshCache();
    int newLine = std::clamp(startLine, 0, static_cast<int>(m_lineStarts.size()) - 1);
    size_t target = vimFirstNonBlank(newLine);
    m_cursors[0] = target;
    m_selectionAnchors[0] = target;
    ensureCursorVisible();
    snapAnimationToTarget();
    update();
}

void EditorViewport::vimYankLines(int startLine, int count) {
    startLine = std::clamp(startLine, 0, static_cast<int>(m_lineStarts.size()) - 1);
    int endLine = std::clamp(startLine + count - 1, startLine, static_cast<int>(m_lineStarts.size()) - 1);
    size_t start = static_cast<size_t>(m_lineStarts[startLine]);
    size_t end = (endLine + 1 < m_lineStarts.size()) ? static_cast<size_t>(m_lineStarts[endLine + 1])
                                                      : static_cast<size_t>(m_cache.size());
    QByteArray text = m_cache.mid(static_cast<int>(start), static_cast<int>(end - start));
    QGuiApplication::clipboard()->setText(QString::fromUtf8(text));
    m_vimLastYankWasLinewise = true;
    size_t target = vimFirstNonBlank(startLine);
    m_cursors[0] = target;
    m_selectionAnchors[0] = target;
    ensureCursorVisible();
    update();
}

void EditorViewport::vimPasteAfter() {
    QString clip = QGuiApplication::clipboard()->text();
    if (clip.isEmpty()) {
        return;
    }
    QByteArray bytes = clip.toUtf8();
    size_t insertAt;
    if (m_vimLastYankWasLinewise) {
        int line = lineForOffset(m_cursors[0]);
        insertAt = (line + 1 < m_lineStarts.size()) ? static_cast<size_t>(m_lineStarts[line + 1])
                                                     : static_cast<size_t>(m_cache.size());
        if (insertAt == static_cast<size_t>(m_cache.size()) && !bytes.endsWith('\n')) {
            bytes.append('\n');
        }
    } else {
        /* At end-of-line, this editor's own cursor convention already
         * sits *at* the '\n' byte (an insertion gap, not "on" a real
         * character — a named fidelity gap vs. real vim, see
         * docs/adr/0046). vimNextCharBoundary assumes its argument
         * points at a real character to step past; blindly applying it
         * here would step over the '\n' itself and land the paste at
         * the start of the *next* line instead of appending it to the
         * end of the current one. */
        size_t cur = m_cursors[0];
        bool atNewline = cur < static_cast<size_t>(m_cache.size()) && m_cache[static_cast<int>(cur)] == '\n';
        insertAt = atNewline ? cur : vimNextCharBoundary(cur);
    }
    ase_undo_begin_group(m_undo, m_cursors.constData(), static_cast<size_t>(m_cursors.size()));
    if (ase_buffer_insert(m_buffer, insertAt, bytes.constData(), static_cast<size_t>(bytes.size()))) {
        ase_undo_record_insert(m_undo, insertAt, bytes.constData(), static_cast<size_t>(bytes.size()));
    }
    ase_undo_end_group(m_undo, m_cursors.constData(), static_cast<size_t>(m_cursors.size()));
    m_dirty = true;
    refreshCache();
    size_t target = m_vimLastYankWasLinewise ? vimFirstNonBlank(lineForOffset(insertAt)) : insertAt;
    m_cursors[0] = target;
    m_selectionAnchors[0] = target;
    ensureCursorVisible();
    snapAnimationToTarget();
    update();
}

void EditorViewport::vimPasteBefore() {
    QString clip = QGuiApplication::clipboard()->text();
    if (clip.isEmpty()) {
        return;
    }
    QByteArray bytes = clip.toUtf8();
    size_t insertAt = m_vimLastYankWasLinewise ? static_cast<size_t>(m_lineStarts[lineForOffset(m_cursors[0])])
                                                : m_cursors[0];
    ase_undo_begin_group(m_undo, m_cursors.constData(), static_cast<size_t>(m_cursors.size()));
    if (ase_buffer_insert(m_buffer, insertAt, bytes.constData(), static_cast<size_t>(bytes.size()))) {
        ase_undo_record_insert(m_undo, insertAt, bytes.constData(), static_cast<size_t>(bytes.size()));
    }
    ase_undo_end_group(m_undo, m_cursors.constData(), static_cast<size_t>(m_cursors.size()));
    m_dirty = true;
    refreshCache();
    size_t target = m_vimLastYankWasLinewise ? vimFirstNonBlank(lineForOffset(insertAt)) : insertAt;
    m_cursors[0] = target;
    m_selectionAnchors[0] = target;
    ensureCursorVisible();
    snapAnimationToTarget();
    update();
}

void EditorViewport::vimOpenLineAbove() {
    int line = lineForOffset(m_cursors[0]);
    size_t at = static_cast<size_t>(m_lineStarts[line]);
    ase_undo_begin_group(m_undo, m_cursors.constData(), static_cast<size_t>(m_cursors.size()));
    if (ase_buffer_insert(m_buffer, at, "\n", 1)) {
        ase_undo_record_insert(m_undo, at, "\n", 1);
    }
    ase_undo_end_group(m_undo, m_cursors.constData(), static_cast<size_t>(m_cursors.size()));
    m_cursors[0] = at;
    m_selectionAnchors[0] = at;
    m_dirty = true;
    refreshCache();
    ensureCursorVisible();
    snapAnimationToTarget();
    update();
}

QString EditorViewport::vimBlockGlyphAt(size_t cursor, int *width, AseHighlightCapture *capture) const {
    int line = lineForOffset(cursor);
    int lineStart = m_lineStarts[line];
    int lineEnd = (line + 1 < m_lineStarts.size()) ? m_lineStarts[line + 1] - 1 : static_cast<int>(m_cache.size());
    int col = columnForOffset(cursor, line);
    /* vimNextCharBoundary steps by codepoint, not by byte, so a
     * multi-byte UTF-8 character under the cursor is measured/drawn
     * whole rather than split mid-sequence. At end of line/buffer it
     * returns `cursor` unchanged (nothing to step past), which is
     * exactly the "no real character here" signal below. */
    size_t glyphEnd = vimNextCharBoundary(cursor);
    int endCol = std::min(static_cast<int>(glyphEnd) - lineStart, lineEnd - lineStart);
    if (endCol <= col) {
        *width = m_charWidth;
        *capture = ASE_HL_NONE;
        return QString();
    }

    QVector<AseHighlightCapture> captures = capturesForLine(lineStart, lineEnd);
    *capture = captures[col];
    *width = xForColumn(lineStart, lineEnd, endCol) - xForColumn(lineStart, lineEnd, col);
    return QString::fromUtf8(m_cache.constData() + lineStart + col, endCol - col);
}

/* Top-level Normal/Visual key dispatcher — see docs/adr/0046 for the
 * full state-machine table. Returns true for every key Vim claims,
 * including "recognized but currently invalid, swallowed" cases;
 * false only for keys outside Vim's alphabet entirely (arrows, Home/
 * End, every Ctrl/Alt/Meta combo, function keys), which the caller's
 * own switch/Ctrl-chain still handles unmodified. */
bool EditorViewport::handleVimNormalOrVisualKey(QKeyEvent *event) {
    if (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) {
        return false;
    }
    if (m_cursors.size() > 1) {
        collapseToOneCursor();
    }

    if (event->key() == Qt::Key_Backspace) {
        vimExecuteMotion('h', std::max(1, m_vimCount1) * std::max(1, m_vimCount2));
        resetVimPendingState();
        ensureCursorVisible();
        update();
        return true;
    }

    QString text = event->text();
    if (text.isEmpty()) {
        return false;
    }
    QChar qc = text.at(0);

    /* Mid-"gg": resolved on the very next key, whatever it is. */
    if (m_vimPendingG) {
        m_vimPendingG = false;
        if (qc == QLatin1Char('g')) {
            int targetLine = (m_vimCount1 > 0) ? (m_vimCount1 - 1) : 0;
            vimGotoLine(targetLine);
        }
        resetVimPendingState();
        ensureCursorVisible();
        snapAnimationToTarget();
        update();
        return true;
    }

    /* Counts. A bare '0' (no count typed yet) is the "column 0" motion,
     * not a digit — matches real vim. */
    if (qc.isDigit()) {
        int d = qc.digitValue();
        int &count = (m_vimPendingOperator != '\0') ? m_vimCount2 : m_vimCount1;
        if (!(d == 0 && count == 0)) {
            count = count * 10 + d;
            return true;
        }
    }

    char c = qc.toLatin1(); /* '\0' for non-Latin1 — falls through to "unrecognized" below. */
    int count = std::max(1, m_vimCount1) * std::max(1, m_vimCount2);

    if (c == 'g') {
        m_vimPendingG = true;
        return true;
    }
    if (c == 'G') {
        int targetLine = (m_vimCount1 > 0) ? (m_vimCount1 - 1) : static_cast<int>(m_lineStarts.size()) - 1;
        vimGotoLine(targetLine);
        resetVimPendingState();
        ensureCursorVisible();
        snapAnimationToTarget();
        update();
        return true;
    }
    if (c == 'h' || c == 'l' || c == 'j' || c == 'k' || c == '0' || c == '^' || c == '$' || c == 'w' ||
        c == 'b' || c == 'e') {
        vimExecuteMotion(c, count);
        resetVimPendingState();
        ensureCursorVisible();
        snapAnimationToTarget();
        update();
        return true;
    }

    if (m_vimMode == VimMode::Visual) {
        switch (c) {
        case 'v':
            collapseToOneCursor();
            m_vimMode = VimMode::Normal;
            break;
        case 'x':
        case 'd':
            if (hasSelectionAt(0)) {
                vimDeleteRange(selectionMinAt(0), selectionMaxAt(0));
            }
            m_vimMode = VimMode::Normal;
            break;
        case 'y':
            if (hasSelectionAt(0)) {
                vimYankRange(selectionMinAt(0), selectionMaxAt(0), false);
            }
            collapseToOneCursor();
            m_vimMode = VimMode::Normal;
            break;
        case 'c':
            if (hasSelectionAt(0)) {
                vimChangeRange(selectionMinAt(0), selectionMaxAt(0));
            } else {
                m_vimMode = VimMode::Insert;
            }
            break;
        default:
            break; /* unrecognized in Visual: swallowed, no state change */
        }
        resetVimPendingState();
        ensureCursorVisible();
        snapAnimationToTarget();
        update();
        return true;
    }

    /* Normal-mode-only from here: operators, x/p/P/u, mode entry. */

    if (c == 'd' || c == 'y' || c == 'c') {
        if (m_vimPendingOperator == c) {
            vimApplyPendingOperatorLinewise(lineForOffset(m_cursors[0]), count);
        } else if (m_vimPendingOperator == '\0') {
            m_vimPendingOperator = c;
        } else {
            resetVimPendingState();
        }
        ensureCursorVisible();
        snapAnimationToTarget();
        update();
        return true;
    }

    if (m_vimPendingOperator != '\0') {
        /* An operator is pending but this key is neither the same
         * operator repeated nor a recognized motion — an invalid
         * combo. Real vim also just does nothing here. */
        resetVimPendingState();
        return true;
    }

    switch (c) {
    case 'v':
        m_vimMode = VimMode::Visual;
        break;
    case 'i':
        m_vimMode = VimMode::Insert;
        break;
    case 'a':
        moveCursorRightAt(0, false);
        m_vimMode = VimMode::Insert;
        break;
    case 'I': {
        size_t target = vimFirstNonBlank(lineForOffset(m_cursors[0]));
        m_cursors[0] = target;
        m_selectionAnchors[0] = target;
        m_vimMode = VimMode::Insert;
        break;
    }
    case 'A':
        moveCursorEndAt(0, false);
        m_vimMode = VimMode::Insert;
        break;
    case 'o':
        moveCursorEndAt(0, false);
        insertText(QByteArrayLiteral("\n"));
        m_vimMode = VimMode::Insert;
        break;
    case 'O':
        vimOpenLineAbove();
        m_vimMode = VimMode::Insert;
        break;
    case 'x': {
        size_t start = m_cursors[0];
        size_t end = start;
        for (int n = 0; n < count && end < static_cast<size_t>(m_cache.size()); ++n) {
            end = vimNextCharBoundary(end);
        }
        if (end > start) {
            vimDeleteRange(start, end);
        }
        break;
    }
    case 'p':
        vimPasteAfter();
        break;
    case 'P':
        vimPasteBefore();
        break;
    case 'u':
        undo();
        break;
    default:
        break; /* unrecognized: swallowed, no state change */
    }

    resetVimPendingState();
    ensureCursorVisible();
    snapAnimationToTarget();
    update();
    return true;
}

QString EditorViewport::primarySelectionText() const {
    int i = m_cursors.size() - 1;
    if (i < 0 || !hasSelectionAt(i)) {
        return QString();
    }
    size_t start = selectionMinAt(i);
    size_t end = selectionMaxAt(i);
    return QString::fromUtf8(m_cache.constData() + static_cast<int>(start), static_cast<int>(end - start));
}

/* Plain substring, ASCII-case-insensitive: fresh QByteArray::toLower()
 * copies of both needle and haystack every call — cheap enough at this
 * project's scale, and toLower() is byte-wise ASCII-only, so it never
 * changes a match's byte length (non-ASCII bytes pass through
 * unchanged). Matches are non-overlapping. Doesn't move the cursor or
 * touch m_currentMatch's *value* beyond clamping it back into range —
 * callers (setFindQuery, findNext/Previous, replace*) decide whether to
 * actually jump. See docs/adr/0021. */
void EditorViewport::recomputeMatches() {
    m_matches.clear();
    if (m_findNeedle.isEmpty()) {
        m_currentMatch = -1;
        return;
    }

    QByteArray needleLower = m_findNeedle.toLower();
    QByteArray hayLower = m_cache.toLower();
    int needleLen = needleLower.size();
    int pos = 0;
    while (true) {
        int idx = hayLower.indexOf(needleLower, pos);
        if (idx < 0) {
            break;
        }
        m_matches.push_back(static_cast<size_t>(idx));
        pos = idx + needleLen;
    }

    if (m_currentMatch >= m_matches.size()) {
        m_currentMatch = m_matches.isEmpty() ? -1 : m_matches.size() - 1;
    }
}

int EditorViewport::nearestMatchAtOrAfter(size_t offset) const {
    for (int i = 0; i < m_matches.size(); ++i) {
        if (m_matches[i] >= offset) {
            return i;
        }
    }
    return 0; /* nothing at/after offset — wrap to the first match */
}

/* Selects m_matches[index] (wrapping either direction) like any other
 * selection, so replaceCurrentMatch can just reuse insertText's existing
 * selection-replace path instead of its own delete/insert logic. */
void EditorViewport::jumpToMatch(int index) {
    if (m_matches.isEmpty()) {
        return;
    }
    m_currentMatch = ((index % m_matches.size()) + m_matches.size()) % m_matches.size();
    size_t start = m_matches[m_currentMatch];
    size_t end = start + static_cast<size_t>(m_findNeedle.size());
    m_cursors = {end};
    m_selectionAnchors = {start};
    m_desiredColumn = -1;
    resetCaretBlink();
    ensureCursorVisible();
    update();
}

/* Recomputes the match list for `needle` and, if anything matches, jumps
 * to the nearest one at/after the current cursor (wrapping to the first
 * match otherwise) — the usual "start typing, land on the nearest hit"
 * incremental-search feel. Called on every keystroke in FindBar's find
 * field. */
void EditorViewport::setFindQuery(const QString &needle) {
    m_findNeedle = needle.toUtf8();
    recomputeMatches();
    if (!m_matches.isEmpty()) {
        jumpToMatch(nearestMatchAtOrAfter(m_cursors.isEmpty() ? 0 : m_cursors[0]));
    } else {
        m_currentMatch = -1;
        update();
    }
}

void EditorViewport::clearFindQuery() {
    m_findNeedle.clear();
    m_matches.clear();
    m_currentMatch = -1;
    update();
}

void EditorViewport::findNext() {
    if (m_matches.isEmpty()) {
        return;
    }
    jumpToMatch(m_currentMatch < 0 ? 0 : m_currentMatch + 1);
}

void EditorViewport::findPrevious() {
    if (m_matches.isEmpty()) {
        return;
    }
    jumpToMatch(m_currentMatch < 0 ? -1 : m_currentMatch - 1);
}

/* The current match is already selected (jumpToMatch put it there), so
 * this is just insertText over an active selection — same path typing
 * over any other selection takes, including its undo group. Advances to
 * whatever match now sits at/after the replacement point. */
void EditorViewport::replaceCurrentMatch(const QByteArray &replacement) {
    if (m_currentMatch < 0 || m_matches.isEmpty()) {
        return;
    }
    insertText(replacement);
    if (!m_matches.isEmpty()) {
        jumpToMatch(nearestMatchAtOrAfter(m_cursors.isEmpty() ? 0 : m_cursors[0]));
    } else {
        m_currentMatch = -1;
        ensureCursorVisible();
    }
    snapAnimationToTarget();
    update();
}

/* Every match, highest-offset-first, as one undo group — the same
 * discipline every other multi-offset batch edit in this file already
 * follows (ADR 0012, ADR 0018). Unlike insertTextAt's selection-replace
 * reuse, this writes directly through ase_buffer_delete/_insert since
 * there's no per-cursor selection here, just a flat offset list. Leaves
 * the cursor at the start of the buffer — v1 simplification, documented
 * in docs/adr/0021, rather than tracking where the "same" text ended up
 * post-replacement. */
void EditorViewport::replaceAllMatches(const QByteArray &replacement) {
    if (m_matches.isEmpty()) {
        return;
    }
    size_t needleLen = static_cast<size_t>(m_findNeedle.size());
    ase_undo_begin_group(m_undo, m_cursors.constData(), static_cast<size_t>(m_cursors.size()));
    for (int i = m_matches.size() - 1; i >= 0; --i) {
        size_t start = m_matches[i];
        QByteArray removed = m_cache.mid(static_cast<int>(start), static_cast<int>(needleLen));
        if (!ase_buffer_delete(m_buffer, start, needleLen)) {
            continue;
        }
        ase_undo_record_delete(m_undo, start, removed.constData(), static_cast<size_t>(removed.size()));
        if (!replacement.isEmpty() &&
            ase_buffer_insert(m_buffer, start, replacement.constData(), static_cast<size_t>(replacement.size()))) {
            ase_undo_record_insert(m_undo, start, replacement.constData(), static_cast<size_t>(replacement.size()));
        }
    }
    m_cursors = {0};
    m_selectionAnchors = {0};
    ase_undo_end_group(m_undo, m_cursors.constData(), static_cast<size_t>(m_cursors.size()));
    m_dirty = true;
    refreshCache(); /* recomputes m_matches too */
    m_currentMatch = m_matches.isEmpty() ? -1 : 0;
    ensureCursorVisible();
    snapAnimationToTarget();
    update();
}

void EditorViewport::insertText(const QByteArray &bytes) {
    if (bytes.isEmpty()) {
        return;
    }
    ase_undo_begin_group(m_undo, m_cursors.constData(), static_cast<size_t>(m_cursors.size()));
    for (int i = m_cursors.size() - 1; i >= 0; --i) {
        insertTextAt(i, bytes);
    }
    normalizeCursors();
    ase_undo_end_group(m_undo, m_cursors.constData(), static_cast<size_t>(m_cursors.size()));
    m_dirty = true;
    refreshCache();
}

/* An active selection is replaced: delete it first (as part of the same
 * undo group), then insert at the collapse point — see docs/adr/0019. */
void EditorViewport::insertTextAt(int i, const QByteArray &bytes) {
    if (hasSelectionAt(i)) {
        deleteSelectionAt(i);
    }
    size_t &cursor = m_cursors[i];
    if (ase_buffer_insert(m_buffer, cursor, bytes.constData(), static_cast<size_t>(bytes.size()))) {
        ase_undo_record_insert(m_undo, cursor, bytes.constData(), static_cast<size_t>(bytes.size()));
        cursor += static_cast<size_t>(bytes.size());
    }
    m_selectionAnchors[i] = cursor;
}

void EditorViewport::deleteBackward() {
    ase_undo_begin_group(m_undo, m_cursors.constData(), static_cast<size_t>(m_cursors.size()));
    for (int i = m_cursors.size() - 1; i >= 0; --i) {
        deleteBackwardAt(i);
    }
    normalizeCursors();
    ase_undo_end_group(m_undo, m_cursors.constData(), static_cast<size_t>(m_cursors.size()));
    m_dirty = true;
    refreshCache();
}

/* m_cache still mirrors the buffer as it stood *before this whole batch*
 * (refreshCache() only runs once, after every cursor in the loop has
 * been processed) — but reading the about-to-be-deleted bytes out of it
 * here is still correct: cursors are processed highest-offset-first, so
 * by the time this particular cursor's [start, cursor) range is touched,
 * no earlier step in the loop could have written into it (only ranges at
 * or above this cursor's own offset could have moved, per the same
 * invariant that already lets deleteBackwardAt skip cross-cursor
 * bookkeeping — see docs/adr/0012). See docs/adr/0018 for why the undo
 * stack needs this snapshot at all: ase_buffer_delete doesn't hand back
 * what it removed. An active selection is the whole operation — no extra
 * character is removed beyond it — see docs/adr/0019. */
void EditorViewport::deleteBackwardAt(int i) {
    if (hasSelectionAt(i)) {
        deleteSelectionAt(i);
        return;
    }
    size_t &cursor = m_cursors[i];
    if (cursor == 0) {
        return;
    }
    size_t start = cursor - 1;
    while (start > 0 && isUtf8ContinuationByte(m_cache[static_cast<int>(start)])) {
        start--;
    }
    QByteArray removed = m_cache.mid(static_cast<int>(start), static_cast<int>(cursor - start));
    if (ase_buffer_delete(m_buffer, start, cursor - start)) {
        ase_undo_record_delete(m_undo, start, removed.constData(), static_cast<size_t>(removed.size()));
        cursor = start;
    }
    m_selectionAnchors[i] = cursor;
}

void EditorViewport::deleteForward() {
    ase_undo_begin_group(m_undo, m_cursors.constData(), static_cast<size_t>(m_cursors.size()));
    for (int i = m_cursors.size() - 1; i >= 0; --i) {
        deleteForwardAt(i);
    }
    normalizeCursors();
    ase_undo_end_group(m_undo, m_cursors.constData(), static_cast<size_t>(m_cursors.size()));
    m_dirty = true;
    refreshCache();
}

void EditorViewport::deleteForwardAt(int i) {
    if (hasSelectionAt(i)) {
        deleteSelectionAt(i);
        return;
    }
    size_t &cursor = m_cursors[i];
    size_t len = static_cast<size_t>(m_cache.size());
    if (cursor >= len) {
        return;
    }
    size_t end = cursor + 1;
    while (end < len && isUtf8ContinuationByte(m_cache[static_cast<int>(end)])) {
        end++;
    }
    QByteArray removed = m_cache.mid(static_cast<int>(cursor), static_cast<int>(end - cursor));
    if (ase_buffer_delete(m_buffer, cursor, end - cursor)) {
        ase_undo_record_delete(m_undo, cursor, removed.constData(), static_cast<size_t>(removed.size()));
    }
    m_selectionAnchors[i] = cursor;
}

void EditorViewport::moveCursorLeft(bool extend) {
    for (int i = 0; i < m_cursors.size(); ++i) {
        moveCursorLeftAt(i, extend);
    }
    normalizeCursors();
}

void EditorViewport::moveCursorLeftAt(int i, bool extend) {
    size_t &cursor = m_cursors[i];
    if (!extend && hasSelectionAt(i)) {
        cursor = selectionMinAt(i);
    } else if (cursor > 0) {
        size_t pos = cursor - 1;
        while (pos > 0 && isUtf8ContinuationByte(m_cache[static_cast<int>(pos)])) {
            pos--;
        }
        cursor = pos;
    }
    if (!extend) {
        m_selectionAnchors[i] = cursor;
    }
}

void EditorViewport::moveCursorRight(bool extend) {
    for (int i = 0; i < m_cursors.size(); ++i) {
        moveCursorRightAt(i, extend);
    }
    normalizeCursors();
}

void EditorViewport::moveCursorRightAt(int i, bool extend) {
    size_t &cursor = m_cursors[i];
    if (!extend && hasSelectionAt(i)) {
        cursor = selectionMaxAt(i);
    } else {
        size_t len = static_cast<size_t>(m_cache.size());
        if (cursor < len) {
            size_t pos = cursor + 1;
            while (pos < len && isUtf8ContinuationByte(m_cache[static_cast<int>(pos)])) {
                pos++;
            }
            cursor = pos;
        }
    }
    if (!extend) {
        m_selectionAnchors[i] = cursor;
    }
}

void EditorViewport::moveCursorVertically(int lineDelta, bool extend) {
    if (m_cursors.size() == 1) {
        /* sticky column — see docs/adr/0012, decision 1 */
        size_t &cursor = m_cursors[0];
        if (!extend && hasSelectionAt(0)) {
            cursor = (lineDelta < 0) ? selectionMinAt(0) : selectionMaxAt(0);
            m_selectionAnchors[0] = cursor;
            m_desiredColumn = -1;
            return;
        }

        int line = lineForOffset(cursor);
        int column = (m_desiredColumn >= 0) ? m_desiredColumn : columnForOffset(cursor, line);
        m_desiredColumn = column;

        int newLine = line + lineDelta;
        if (newLine >= 0 && newLine < m_lineStarts.size()) {
            cursor = offsetForLineColumn(newLine, column);
        }
        if (!extend) {
            m_selectionAnchors[0] = cursor;
        }
        return;
    }

    for (int i = 0; i < m_cursors.size(); ++i) {
        moveCursorVerticallyAt(i, lineDelta, extend);
    }
    normalizeCursors();
}

void EditorViewport::moveCursorVerticallyAt(int i, int lineDelta, bool extend) {
    size_t &cursor = m_cursors[i];
    if (!extend && hasSelectionAt(i)) {
        cursor = (lineDelta < 0) ? selectionMinAt(i) : selectionMaxAt(i);
    } else {
        int line = lineForOffset(cursor);
        int column = columnForOffset(cursor, line);
        int newLine = line + lineDelta;
        if (newLine >= 0 && newLine < m_lineStarts.size()) {
            cursor = offsetForLineColumn(newLine, column);
        }
    }
    if (!extend) {
        m_selectionAnchors[i] = cursor;
    }
}

void EditorViewport::moveCursorHome(bool extend) {
    for (int i = 0; i < m_cursors.size(); ++i) {
        moveCursorHomeAt(i, extend);
    }
    normalizeCursors();
}

void EditorViewport::moveCursorHomeAt(int i, bool extend) {
    size_t &cursor = m_cursors[i];
    if (!extend && hasSelectionAt(i)) {
        cursor = selectionMinAt(i);
    } else {
        int line = lineForOffset(cursor);
        cursor = static_cast<size_t>(m_lineStarts[line]);
    }
    if (!extend) {
        m_selectionAnchors[i] = cursor;
    }
}

void EditorViewport::moveCursorEnd(bool extend) {
    for (int i = 0; i < m_cursors.size(); ++i) {
        moveCursorEndAt(i, extend);
    }
    normalizeCursors();
}

void EditorViewport::moveCursorEndAt(int i, bool extend) {
    size_t &cursor = m_cursors[i];
    if (!extend && hasSelectionAt(i)) {
        cursor = selectionMaxAt(i);
    } else {
        int line = lineForOffset(cursor);
        int end = (line + 1 < m_lineStarts.size()) ? m_lineStarts[line + 1] - 1 : static_cast<int>(m_cache.size());
        cursor = static_cast<size_t>(end);
    }
    if (!extend) {
        m_selectionAnchors[i] = cursor;
    }
}

void EditorViewport::ensureCursorVisible() {
    size_t cursor = m_cursors.last();
    int line = lineForOffset(cursor);
    int visibleLines = std::max(1, height() / m_lineHeight);
    /* Clamped so the margin can never exceed half the viewport — a
     * short/narrow window degrades to less context instead of
     * oscillating or refusing to scroll. See docs/adr/0024. */
    int vMargin = std::min(kVerticalScrollMargin, std::max(0, (visibleLines - 1) / 2));
    if (line < m_scrollLine + vMargin) {
        m_scrollLine = line - vMargin;
    } else if (line >= m_scrollLine + visibleLines - vMargin) {
        m_scrollLine = line - visibleLines + 1 + vMargin;
    }
    m_scrollLine = std::max(0, m_scrollLine);

    /* Horizontal half, symmetric to the vertical logic above — see
     * docs/adr/0014, decision 3. */
    int lineStart = m_lineStarts[line];
    int lineEnd = (line + 1 < m_lineStarts.size()) ? m_lineStarts[line + 1] - 1 : static_cast<int>(m_cache.size());
    int col = columnForOffset(cursor, line);
    int caretX = xForColumn(lineStart, lineEnd, col);

    int textAreaWidth = std::max(1, width() - gutterWidth());
    int hMargin = std::min(m_charWidth * kHorizontalScrollMarginChars, std::max(0, (textAreaWidth - kCaretWidth) / 2));
    if (caretX < m_scrollX + hMargin) {
        m_scrollX = caretX - hMargin;
    } else if (caretX + kCaretWidth > m_scrollX + textAreaWidth - hMargin) {
        m_scrollX = caretX + kCaretWidth - textAreaWidth + hMargin;
    }
    m_scrollX = std::max(0, m_scrollX);

    /* 1-based for display — every cursor move and every edit already
     * ends up here, so this is the one place status needs wiring. See
     * docs/adr/0023. modeLabel is empty whenever Vim mode is off, so
     * the status text is byte-identical to before docs/adr/0046 for
     * anyone who hasn't opted in. */
    QString modeLabel;
    if (vimModeActive()) {
        switch (m_vimMode) {
        case VimMode::Normal:
            modeLabel = QStringLiteral("NORMAL");
            break;
        case VimMode::Visual:
            modeLabel = QStringLiteral("VISUAL");
            break;
        case VimMode::Insert:
            modeLabel = QStringLiteral("INSERT");
            break;
        }
    }
    emit statusChanged(line + 1, col + 1, m_dirty, modeLabel);
}

/* Empty m_filePath (launched with no file, or a fresh openFile that
 * failed to read one) routes to the Save-As panel instead of silently
 * doing nothing — the one gap ADR 0006 flagged as deferred. */
void EditorViewport::save() {
    if (m_filePath.isEmpty()) {
        if (m_fileBrowser != nullptr) {
            m_fileBrowser->openFor(FileBrowserPanel::Mode::SaveAs);
        }
        return;
    }
    if (ase_buffer_save_to_file(m_buffer, m_filePath.toUtf8().constData())) {
        m_dirty = false;
        ensureCursorVisible(); /* pushes the cleared dirty flag (and title) through statusChanged */
    }
}

/* Destroys the current buffer/syntax/undo-history and loads `path`
 * fresh, resetting every piece of per-buffer state — cursors, scroll,
 * find query, dirty flag. Missing/unreadable files start empty with
 * `path` kept as the save target, same tolerance
 * ase_buffer_create_from_file's caller in main.cpp already had for the
 * initial launch (docs/adr/0006) — opening a not-yet-existing file by
 * name is a normal editor action, not an error. */
void EditorViewport::openFile(const QString &path) {
    /* The old client (if any) is tied to the old file's URI — stop it
     * before refreshCache() below can send it a stale-URI didChange,
     * and clear its diagnostics rather than leave them drawn against
     * the new file's unrelated content. startLspClientIfConfigured()
     * at the end starts a fresh one for the new file, same as the
     * constructor does for the initial one. */
    ase_lsp_client_stop(m_lspClient);
    m_lspClient = nullptr;
    m_diagnostics.clear();

    ase_syntax_destroy(m_syntax);
    m_syntax = nullptr;
    ase_undo_destroy(m_undo);
    ase_buffer_destroy(m_buffer);

    AseBuffer *buffer = ase_buffer_create_from_file(path.toUtf8().constData());
    if (buffer == nullptr) {
        buffer = ase_buffer_create();
    }
    m_buffer = buffer;
    m_undo = ase_undo_create();
    m_filePath = path;

    QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("c") || suffix == QLatin1String("h")) {
        m_syntax = ase_syntax_create_c();
    }

    m_cursors = {0};
    m_selectionAnchors = {0};
    m_scrollLine = 0;
    m_scrollX = 0;
    m_desiredColumn = -1;
    clearFindQuery();
    m_dirty = false;

    refreshCache();
    startLspClientIfConfigured();
    ensureCursorVisible();
    resetCaretBlink();
    snapAnimationToTarget();
    update();
}

/* Sets the save target then defers to save() itself, so dirty-clearing
 * and the statusChanged emit (title included, via filePath()) happen
 * in exactly one place rather than being duplicated here. */
void EditorViewport::saveAs(const QString &path) {
    m_filePath = path;
    save();
}

void EditorViewport::runCommand(const QString &command) {
    QString trimmed = command.trimmed();
    if (trimmed == QLatin1String("w")) {
        save();
    } else if (trimmed == QLatin1String("q")) {
        window()->close();
    } else if (trimmed == QLatin1String("compile")) {
        compile();
    } else if (trimmed == QLatin1String("output")) {
        toggleOutputPanel();
    } else {
        /* :<digits> — jump to that 1-based line. Not gated on vim_mode:
         * a generically useful command line addition, and the first one
         * to take an argument rather than match an exact string — see
         * docs/adr/0046. Reuses vimGotoLine (safe regardless of vim_mode:
         * no operator can be pending here since ex-commands don't go
         * through Vim's own key dispatch at all). */
        bool ok = false;
        int lineNumber = trimmed.toInt(&ok);
        if (ok && lineNumber > 0) {
            collapseToOneCursor();
            vimGotoLine(lineNumber - 1);
            resetVimPendingState();
            ensureCursorVisible();
            snapAnimationToTarget();
            update();
        }
        /* Anything else recognized as neither a known word nor a line
         * number: silent no-op — see docs/adr/0025. */
    }
}

void EditorViewport::toggleOutputPanel() {
    if (m_outputPanel != nullptr) {
        m_outputPanel->setVisible(!m_outputPanel->isVisible());
    }
}

/* Reads build_command fresh from config on every call (not cached) so
 * an edited config.ase takes effect on the next :compile without a
 * restart, same hot-reload spirit as everything else config-driven in
 * this class. */
void EditorViewport::compile() {
    if (m_outputPanel == nullptr) {
        return;
    }
    if (m_compileProcess != nullptr) {
        m_outputPanel->appendLine(QStringLiteral("A build is already running."));
        m_outputPanel->show();
        return;
    }

    const char *buildCommand = ase_config_get_string(m_config, "build_command");
    if (buildCommand == nullptr) {
        m_outputPanel->appendLine(QStringLiteral("No build_command configured — see config.ase."));
        m_outputPanel->show();
        return;
    }
    if (m_filePath.isEmpty()) {
        m_outputPanel->appendLine(QStringLiteral("No file to compile — save it first."));
        m_outputPanel->show();
        return;
    }

    QString substituted = QString::fromUtf8(buildCommand).replace(QLatin1String("%f"), m_filePath);
    QByteArray substitutedUtf8 = substituted.toUtf8();
    QByteArray cwdUtf8 = QFileInfo(m_filePath).absolutePath().toUtf8();

    /* Run through a shell, not execvp'd directly — build_command is
     * documented (config.c's starter template) as a shell command, so
     * it can use `&&`/pipes/etc., the same way :compile's config-key
     * comment shows. */
    const char *argv[] = {"/bin/sh", "-c", substitutedUtf8.constData(), nullptr};
    m_compileProcess = ase_process_spawn(argv, cwdUtf8.constData());

    m_outputPanel->clear();
    m_outputPanel->show();
    if (m_compileProcess == nullptr) {
        m_outputPanel->appendLine(QStringLiteral("Failed to start build_command."));
        return;
    }
    m_outputPanel->appendLine(QStringLiteral("$ ") + substituted);
    m_compilePollTimer->start(100); /* same non-blocking-poll shape as ase_lsp_client_poll */
}

void EditorViewport::pollCompile() {
    if (m_compileProcess == nullptr) {
        m_compilePollTimer->stop();
        return;
    }

    char buf[4096];
    for (;;) {
        long n = ase_process_read(m_compileProcess, buf, sizeof(buf));
        if (n <= 0) {
            break;
        }
        m_outputPanel->appendText(QString::fromUtf8(buf, static_cast<int>(n)));
    }

    if (ase_process_has_exited(m_compileProcess)) {
        m_outputPanel->appendLine(QStringLiteral("[exit code %1]").arg(ase_process_exit_code(m_compileProcess)));
        ase_process_destroy(m_compileProcess);
        m_compileProcess = nullptr;
        m_compilePollTimer->stop();
    }
}

namespace {
void lspDiagnosticsTrampoline(void *user_data, const char *uri, const AseLspDiagnostic *diagnostics, size_t count) {
    static_cast<EditorViewport *>(user_data)->applyLspDiagnostics(uri, diagnostics, count);
}
void lspCompletionTrampoline(void *user_data, const AseJsonValue *result, const char *error_message) {
    static_cast<EditorViewport *>(user_data)->applyLspCompletion(result, error_message);
}
void lspHoverTrampoline(void *user_data, const AseJsonValue *result, const char *error_message) {
    static_cast<EditorViewport *>(user_data)->applyLspHover(result, error_message);
}
} // namespace

/* Gated the same way Tree-sitter syntax highlighting already is
 * (.c/.h suffix) — a language server for anything else would need a
 * per-language command mapping this v1 doesn't attempt. No default for
 * `lsp_command` (config.c) — unconfigured means no LSP for this
 * session, not a guess at which server is installed. Called from both
 * the constructor and openFile() (which stops any previous client
 * first — see its own call site) — so switching files always talks to
 * a fresh server against the right document. Editing `lsp_command`
 * itself mid-session and hot-reloading is still a documented v1 gap:
 * that only takes effect on the next file open, see docs/adr/0029. */
void EditorViewport::startLspClientIfConfigured() {
    QString suffix = QFileInfo(m_filePath).suffix().toLower();
    if (suffix != QLatin1String("c") && suffix != QLatin1String("h")) {
        return;
    }
    const char *lspCommand = ase_config_get_string(m_config, "lsp_command");
    if (lspCommand == nullptr || m_filePath.isEmpty()) {
        return;
    }

    const char *argv[] = {lspCommand, nullptr};
    /* QUrl::fromLocalFile on a *relative* path (e.g. the editor was
     * launched as `ase_gui file.c` from a shell, not `ase_gui
     * /abs/path/file.c`) produces a malformed URI — real servers
     * (clangd included) reject it outright ("unresolvable URI"),
     * silently breaking every LSP feature. QFileInfo::absoluteFilePath
     * resolves against the current working directory first, matching
     * how the shell itself resolved the relative path at launch. See
     * docs/adr/0032. */
    m_lspUri = QUrl::fromLocalFile(QFileInfo(m_filePath).absoluteFilePath()).toString();
    m_lspClient = ase_lsp_client_start(argv, nullptr);
    if (m_lspClient == nullptr) {
        return;
    }

    ase_lsp_client_set_diagnostics_callback(m_lspClient, lspDiagnosticsTrampoline, this);
    ase_lsp_client_did_open(m_lspClient, m_lspUri.toUtf8().constData(), "c", m_cache.constData());
    m_lspVersion = 1;
}

void EditorViewport::pollLsp() {
    if (m_lspClient != nullptr) {
        ase_lsp_client_poll(m_lspClient);
    }
}

/* Full-document sync — see ase_lsp_client_did_change's own doc
 * comment. Called from refreshCache(), the one choke point every edit
 * already passes through, so this is the fix for "diagnostics go stale
 * after the first edit." m_cache is already a full, current mirror of
 * the buffer (ADR 0006) and, like the rest of this codebase, assumed
 * NUL-free — passing it as a C string needs no extra copy. */
void EditorViewport::sendLspDidChange() {
    if (m_lspClient == nullptr) {
        return;
    }
    m_lspVersion++;
    ase_lsp_client_did_change(m_lspClient, m_lspUri.toUtf8().constData(), m_lspVersion, m_cache.constData());
}

/* The diagnostics callback — see docs/adr/0029. `diagnostics` is
 * borrowed (valid only during this call), so every field that matters
 * is copied out, including `message` (QString, not a stored pointer).
 * A URI for a different document is ignored outright — defensive only,
 * since v1 has exactly one open document per client. */
void EditorViewport::applyLspDiagnostics(const char *uri, const AseLspDiagnostic *diagnostics, size_t count) {
    if (QString::fromUtf8(uri) != m_lspUri) {
        return;
    }
    m_diagnostics.clear();
    m_diagnostics.reserve(static_cast<int>(count));
    for (size_t i = 0; i < count; ++i) {
        GuiDiagnostic d;
        d.startLine = diagnostics[i].start.line;
        d.startChar = diagnostics[i].start.character;
        d.endLine = diagnostics[i].end.line;
        d.endChar = diagnostics[i].end.character;
        d.severity = diagnostics[i].severity;
        d.message = QString::fromUtf8(diagnostics[i].message);
        m_diagnostics.push_back(d);
    }
    update();
}

/* Error red / warning amber, both configurable (docs/adr/0029) — the
 * one deliberate departure from the "one font color" pillar, since
 * severity color-coding is too strong and too expected a convention to
 * fold into opacity/weight the way syntax highlighting does.
 * Information/Hint (severities 3/4) and anything unspecified fall back
 * to the plain text color at low alpha — present, but not competing
 * for attention with an actual error. */
QColor EditorViewport::colorForSeverity(int severity) const {
    if (severity == 1) {
        return m_diagnosticErrorColor;
    }
    if (severity == 2) {
        return m_diagnosticWarningColor;
    }
    QColor c = m_textColor;
    c.setAlpha(140);
    return c;
}

int EditorViewport::worstSeverityForLine(int line) const {
    int worstSeverity = 0;
    for (const GuiDiagnostic &d : m_diagnostics) {
        if (line >= d.startLine && line <= d.endLine && (worstSeverity == 0 || d.severity < worstSeverity)) {
            worstSeverity = d.severity;
        }
    }
    return worstSeverity;
}

double EditorViewport::diagnosticRevealForLine(int line) const {
    for (const DiagnosticLineHighlight &h : m_diagnosticLineHighlights) {
        if (h.line == line) {
            return h.reveal;
        }
    }
    return 0.0;
}

/* See docs/adr/0041. Called every frame, unconditionally — with
 * animations off this still runs, it just snaps reveal straight to
 * target (the same "state is always live, only the transition's
 * smoothness is opt-in" convention paintEvent's gutter current-line-
 * number brightness already uses). */
void EditorViewport::updateDiagnosticLineHighlights() {
    int cursorLine = m_cursors.isEmpty() ? -1 : lineForOffset(m_cursors.last());
    int focusedLine = (cursorLine >= 0 && worstSeverityForLine(cursorLine) != 0) ? cursorLine : -1;

    bool focusedLineTracked = false;
    for (DiagnosticLineHighlight &h : m_diagnosticLineHighlights) {
        if (h.line == focusedLine) {
            h.target = 1.0;
            focusedLineTracked = true;
        } else {
            h.target = 0.0;
        }
    }
    if (focusedLine != -1 && !focusedLineTracked) {
        m_diagnosticLineHighlights.push_back({focusedLine, 0.0, 1.0});
    }

    for (int i = m_diagnosticLineHighlights.size() - 1; i >= 0; --i) {
        DiagnosticLineHighlight &h = m_diagnosticLineHighlights[i];
        double delta = h.target - h.reveal;
        if (!m_animationsEnabled || std::abs(delta) < kOpacitySnapThreshold) {
            h.reveal = h.target;
        } else {
            h.reveal += delta * kEaseFactor;
        }
        if (h.reveal <= 0.0 && h.target == 0.0) {
            m_diagnosticLineHighlights.removeAt(i);
        }
    }
}

void EditorViewport::updateWelcomeOverlayOpacity() {
    double target = m_cache.isEmpty() ? 1.0 : 0.0;
    double delta = target - m_welcomeOverlayOpacity;
    if (!m_animationsEnabled || std::abs(delta) < kOpacitySnapThreshold) {
        m_welcomeOverlayOpacity = target;
    } else {
        m_welcomeOverlayOpacity += delta * kEaseFactor;
    }
}

/* -------------------------------------------------------------- completion */

size_t EditorViewport::completionPrefixStart(size_t offset) const {
    size_t start = offset;
    while (start > 0 && isWordChar(m_cache[static_cast<int>(start) - 1])) {
        start--;
    }
    return start;
}

void EditorViewport::requestCompletionIfAppropriate() {
    if (m_completionPopup == nullptr) {
        return;
    }
    if (vimModeActive() && m_vimMode != VimMode::Insert) {
        /* Vim's own buffer mutations (dw, p, etc.) go through refreshCache()
         * like any other edit, which is what calls this — but a completion
         * popup responding to Normal/Visual-mode edits would be a
         * distraction, not a help. See docs/adr/0046. */
        dismissCompletion();
        return;
    }
    if (m_suppressNextCompletionTrigger) {
        m_suppressNextCompletionTrigger = false;
        dismissCompletion();
        return;
    }
    if (m_lspClient == nullptr || m_cursors.size() != 1 || hasSelectionAt(0)) {
        dismissCompletion();
        return;
    }

    size_t cursor = m_cursors[0];
    bool afterWordChar = cursor > 0 && isWordChar(m_cache[static_cast<int>(cursor) - 1]);
    /* Member-access triggers: '.' or the '>' of "->" (C has no "::").
     * Anything else is treated as "nothing worth completing here" —
     * without this gate, a request (and likely a long list of global
     * symbols) would fire after every space and newline too. */
    bool afterTrigger =
        cursor > 0 && (m_cache[static_cast<int>(cursor) - 1] == '.' ||
                       (cursor > 1 && m_cache[static_cast<int>(cursor) - 1] == '>' &&
                        m_cache[static_cast<int>(cursor) - 2] == '-'));
    if (!afterWordChar && !afterTrigger) {
        dismissCompletion();
        return;
    }

    m_completionPrefixStart = completionPrefixStart(cursor);

    int line = lineForOffset(cursor);
    AseLspPosition pos;
    pos.line = line;
    pos.character = columnForOffset(cursor, line);
    ase_lsp_client_request_completion(m_lspClient, m_lspUri.toUtf8().constData(), pos, lspCompletionTrampoline,
                                       this);
}

void EditorViewport::applyLspCompletion(const AseJsonValue *result, const char *error_message) {
    if (m_completionPopup == nullptr) {
        return;
    }
    if (error_message != nullptr || result == nullptr) {
        dismissCompletion();
        return;
    }
    applyCompletionResult(result);
}

/* `result` is either a bare CompletionItem[] or a CompletionList
 * {isIncomplete, items: [...]}  — both shapes are valid per the LSP
 * spec, servers differ on which they send. No request-sequence
 * tracking — see m_completionPrefixStart's doc comment in the header. */
void EditorViewport::applyCompletionResult(const AseJsonValue *result) {
    const AseJsonValue *itemsArray = result;
    if (ase_json_type(result) == ASE_JSON_OBJECT) {
        const AseJsonValue *items = ase_json_object_get(result, "items");
        if (items != nullptr) {
            itemsArray = items;
        }
    }
    if (ase_json_type(itemsArray) != ASE_JSON_ARRAY || m_cursors.size() != 1) {
        dismissCompletion();
        return;
    }

    QVector<CompletionPopup::Item> popupItems;
    size_t count = ase_json_array_size(itemsArray);
    for (size_t i = 0; i < count && popupItems.size() < 50; ++i) {
        const AseJsonValue *item = ase_json_array_get(itemsArray, i);
        if (item == nullptr || ase_json_type(item) != ASE_JSON_OBJECT) {
            continue;
        }
        const char *label = ase_json_get_string(ase_json_object_get(item, "label"));
        if (label == nullptr) {
            continue;
        }
        GuiCompletionItem gi;
        gi.label = QString::fromUtf8(label);
        const char *insertText = ase_json_get_string(ase_json_object_get(item, "insertText"));
        gi.insertText = (insertText != nullptr) ? QString::fromUtf8(insertText) : gi.label;
        const char *detail = ase_json_get_string(ase_json_object_get(item, "detail"));
        gi.detail = (detail != nullptr) ? QString::fromUtf8(detail) : QString();
        popupItems.push_back({gi.label, gi.insertText, gi.detail});
    }

    if (popupItems.isEmpty()) {
        dismissCompletion();
        return;
    }

    QPointF caret = caretTargetFor(m_cursors[0]);
    QPoint anchor(static_cast<int>(caret.x()), static_cast<int>(caret.y() + m_lineHeight));
    m_completionPopup->showItems(popupItems, anchor);
}

/* Replaces [m_completionPrefixStart, cursor) with the selected item's
 * insertText by giving insertText() a temporary single-cursor selection
 * over that range — it already knows how to replace an active
 * selection (docs/adr/0019), so this just reuses that path instead of
 * duplicating it. */
void EditorViewport::acceptCompletion() {
    if (m_completionPopup == nullptr || !m_completionPopup->isShowingPopup() || m_cursors.size() != 1) {
        return;
    }
    const CompletionPopup::Item *item = m_completionPopup->selectedItem();
    if (item == nullptr) {
        return;
    }

    m_selectionAnchors[0] = std::min(m_completionPrefixStart, m_cursors[0]);
    m_suppressNextCompletionTrigger = true;
    insertText(item->insertText.toUtf8());
    dismissCompletion();
    ensureCursorVisible();
    snapAnimationToTarget(); /* an edit, like typing — see docs/adr/0017 */
    update();
}

void EditorViewport::dismissCompletion() {
    if (m_completionPopup != nullptr) {
        m_completionPopup->dismiss();
    }
}

/* ------------------------------------------------------------------ hover */

void EditorViewport::scheduleHoverRequest(const QPoint &viewportPos) {
    if (m_hoverPanel == nullptr) {
        return;
    }
    size_t offset = offsetForPoint(viewportPos);

    if (m_hoverPanel->isShowingHover() && offset >= m_hoverShownRangeStart && offset < m_hoverShownRangeEnd) {
        /* Still hovering the word the open tooltip covers — no new
         * request needed, but keep it glued to the actual pointer
         * position (it eases there, doesn't jump — see
         * TrackingPopup::retarget) rather than staying pinned to
         * wherever it first appeared. */
        m_hoverPendingPos = viewportPos;
        m_hoverPanel->moveTo(viewportPos);
        return;
    }
    if (m_hoverPanel->isShowingHover()) {
        dismissHover();
    }

    m_hoverPendingPos = viewportPos;
    m_hoverPendingOffset = offset;
    m_hoverTimer->start(500); /* restarts if already running — the pause-before-request delay */
}

void EditorViewport::requestHoverNow() {
    if (m_lspClient == nullptr || m_hoverPanel == nullptr) {
        return;
    }
    int line = lineForOffset(m_hoverPendingOffset);
    AseLspPosition pos;
    pos.line = line;
    pos.character = columnForOffset(m_hoverPendingOffset, line);
    ase_lsp_client_request_hover(m_lspClient, m_lspUri.toUtf8().constData(), pos, lspHoverTrampoline, this);
}

void EditorViewport::applyLspHover(const AseJsonValue *result, const char *error_message) {
    if (m_hoverPanel == nullptr || error_message != nullptr || result == nullptr ||
        ase_json_type(result) != ASE_JSON_OBJECT) {
        /* Doesn't dismiss an already-open tooltip: with no request-
         * sequence tracking (see applyCompletionResult's doc comment),
         * an empty/failed response could belong to an earlier, already-
         * superseded request — silently contributing nothing is safer
         * than possibly clobbering a newer, good tooltip. */
        return;
    }
    applyHoverResult(result);
}

void EditorViewport::applyHoverResult(const AseJsonValue *result) {
    QString text = extractHoverText(ase_json_object_get(result, "contents"));
    if (text.isEmpty()) {
        return;
    }

    /* The response's own `range` (if present) is exactly the word/
     * token the tooltip covers — used to tell "still hovering the same
     * thing" from "moved to something else" next time the mouse moves.
     * Without one, fall back to scanning the identifier run around the
     * requested offset client-side. */
    size_t rangeStart = m_hoverPendingOffset;
    size_t rangeEnd = m_hoverPendingOffset + 1;
    const AseJsonValue *range = ase_json_object_get(result, "range");
    const AseJsonValue *start = (range != nullptr) ? ase_json_object_get(range, "start") : nullptr;
    const AseJsonValue *end = (range != nullptr) ? ase_json_object_get(range, "end") : nullptr;
    if (start != nullptr && end != nullptr) {
        int startLine = static_cast<int>(ase_json_get_number(ase_json_object_get(start, "line"), 0));
        int startChar = static_cast<int>(ase_json_get_number(ase_json_object_get(start, "character"), 0));
        int endLine = static_cast<int>(ase_json_get_number(ase_json_object_get(end, "line"), 0));
        int endChar = static_cast<int>(ase_json_get_number(ase_json_object_get(end, "character"), 0));
        rangeStart = offsetForLineColumn(startLine, startChar);
        rangeEnd = std::max(rangeStart + 1, offsetForLineColumn(endLine, endChar));
    } else {
        rangeStart = completionPrefixStart(m_hoverPendingOffset);
        size_t end2 = m_hoverPendingOffset;
        while (end2 < static_cast<size_t>(m_cache.size()) && isWordChar(m_cache[static_cast<int>(end2)])) {
            end2++;
        }
        rangeEnd = std::max(rangeStart + 1, end2);
    }
    m_hoverShownRangeStart = rangeStart;
    m_hoverShownRangeEnd = rangeEnd;

    m_hoverPanel->showText(text, m_hoverPendingPos);
}

void EditorViewport::dismissHover() {
    m_hoverTimer->stop();
    if (m_hoverPanel != nullptr) {
        m_hoverPanel->dismiss();
    }
}

bool EditorViewport::isModalPanelOpen() const {
    return (m_findBar != nullptr && m_findBar->isVisible()) ||
           (m_fileBrowser != nullptr && m_fileBrowser->isVisible()) ||
           (m_commandLine != nullptr && m_commandLine->isVisible()) ||
           (m_helpPanel != nullptr && m_helpPanel->isVisible()) ||
           (m_aboutPanel != nullptr && m_aboutPanel->isVisible());
}

/* Shared by undo()/redo(): apply the cursor snapshot the undo stack
 * handed back, refresh everything downstream of a buffer mutation, and
 * render instantly (like any other edit — see snapAnimationToTarget's
 * doc comment) rather than gliding. */
void EditorViewport::applyUndoResult(size_t *cursors, size_t count) {
    /* The undo stack only snapshots point offsets (docs/adr/0018), so
     * restoring here always lands with no active selection — matches
     * how typing over a selection collapses it too. */
    m_cursors.clear();
    m_selectionAnchors.clear();
    m_cursors.reserve(static_cast<int>(count));
    m_selectionAnchors.reserve(static_cast<int>(count));
    for (size_t i = 0; i < count; ++i) {
        m_cursors.push_back(cursors[i]);
        m_selectionAnchors.push_back(cursors[i]);
    }
    free(cursors);
    if (m_cursors.isEmpty()) {
        m_cursors.push_back(0);
        m_selectionAnchors.push_back(0);
    }

    /* Any undo/redo marks dirty — simpler than tracking the exact saved
     * stack position, an acceptable v1 gap (docs/adr/0023) since the
     * common case (undo back to a saved state, still see the dirty
     * marker) is a minor cosmetic paper cut, not a data-loss risk. */
    m_dirty = true;
    refreshCache();
    ensureCursorVisible();
    resetCaretBlink();
    snapAnimationToTarget();
    update();
}

void EditorViewport::undo() {
    size_t *cursors = nullptr;
    size_t count = 0;
    if (!ase_undo_undo(m_undo, m_buffer, &cursors, &count)) {
        return;
    }
    applyUndoResult(cursors, count);
}

void EditorViewport::redo() {
    size_t *cursors = nullptr;
    size_t count = 0;
    if (!ase_undo_redo(m_undo, m_buffer, &cursors, &count)) {
        return;
    }
    applyUndoResult(cursors, count);
}

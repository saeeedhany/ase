#include "editor_viewport.h"

#include "editor_viewport_internal.h"
#include "motion.h"

#ifndef ASE_VERSION_STRING
#define ASE_VERSION_STRING "0.0.0-dev" /* fallback if CMake didn't define it — see gui/CMakeLists.txt */
#endif

#include <algorithm>
#include <cmath>

#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace {
/* Caret breathe cycle, ~720ms. */
inline int caretAnimationTicks() { return motion::ticksFor(720); }
constexpr double kTwoPi = 6.283185307179586;
/* A typed character grows from 85% to 100% as it fades in. */
constexpr double kTypingAnimationStartScale = 0.85;
/* A full-line-height caret reads as too blocky. The knocked-out glyph
 * still uses the un-inset rect, to keep its baseline. */
constexpr int kCaretVerticalInset = 2;
constexpr int kGutterPadding = 8; /* on each side of the line-number text */
/* Easing and tick live in motion.h, alongside the chrome tiers. */
using motion::kEaseFactor;

/* The dot's range starts higher than the underline's, so diagnostics
 * stay visible in the gutter even unfocused. See docs/adr/0041. */
constexpr int kDiagnosticUnderlineDimAlpha = 110;
constexpr int kDiagnosticUnderlineFocusAlpha = 255;
constexpr int kDiagnosticDotDimAlpha = 150;
constexpr int kDiagnosticDotFocusAlpha = 255;
using motion::kOpacitySnapThreshold;
} // namespace

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

    /* Normally a no-op: updateAnimation() already did this for the
     * scroll position it settled on. Kept because drawing is what
     * actually requires it. See docs/adr/0072. */
    ensureCaptureWindowForViewport();

    /* Document coordinates from here to restore(): the translate folds
     * in gutter offset, horizontal scroll and the sub-line remainder,
     * and the clip keeps it off the gutter. */
    painter.save();
    painter.translate(gutter - m_renderedScrollX, -fracLine * m_lineHeight);
    painter.setClipRect(QRectF(m_renderedScrollX, -static_cast<double>(m_lineHeight), textAreaWidth,
                                height() + 2.0 * m_lineHeight));

    /* Behind the glyphs, so text stays crisp over the overlay. */
    for (int i = 0; i < m_cursors.size(); ++i) {
        /* Not hasSelectionAt(), which asks whether the anchor and the
         * cursor differ. A linewise selection of one line has both at
         * the start of that line and still covers it, so the question
         * to ask is the one the operators ask: is there a range? */
        size_t start = selectionMinAt(i);
        size_t end = vimVisualEnd(i);
        if (end <= start) {
            continue;
        }
        highlightRange(painter, start, end, firstLine, lastLine, m_selectionColor);
    }

    /* Current match drawn last, so it lands on top of any regular
     * match it overlaps. */
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

    /* Re-paints over the rect drawLine already filled, rather than
     * teaching its run segmentation a third state. See docs/adr/0049. */
    for (const TypingAnimation &anim : m_typingAnimations) {
        if (anim.start + anim.length > static_cast<size_t>(m_cache.size())) {
            continue; /* stale — updateAnimation() drops it next tick */
        }
        int line = lineForOffset(anim.start);
        if (line < firstLine || line >= lastLine) {
            continue; /* off-screen right now; still ticking, just not drawn */
        }
        int lineStart = m_lineStarts[line];
        int lineEnd = (line + 1 < m_lineStarts.size()) ? m_lineStarts[line + 1] - 1 : static_cast<int>(m_cache.size());
        int startCol = static_cast<int>(anim.start) - lineStart;
        int endCol = static_cast<int>(anim.start + anim.length) - lineStart;
        if (endCol > lineEnd - lineStart) {
            continue; /* defensive — shouldn't happen, the '\n' guard at the call site keeps insertions single-line */
        }

        double progress = std::clamp(static_cast<double>(anim.elapsedTicks) / typingAnimationTicks(), 0.0, 1.0);
        double t = 1.0 - (1.0 - progress) * (1.0 - progress); /* ease-out, same shape as kEaseFactor elsewhere */
        double scale = kTypingAnimationStartScale + (1.0 - kTypingAnimationStartScale) * t;

        int x0 = xForColumn(lineStart, lineEnd, startCol);
        int x1 = xForColumn(lineStart, lineEnd, endCol);
        int y = (line - firstLine) * m_lineHeight;
        QRectF rect(x0, y, x1 - x0, m_lineHeight);

        QVector<AseHighlightCapture> captures = capturesForLine(lineStart, lineEnd);
        AseHighlightCapture capture = captures[startCol];
        QString text = QString::fromUtf8(m_cache.constData() + lineStart + startCol, endCol - startCol);
        QColor color = colorForCapture(capture);
        color.setAlpha(static_cast<int>(color.alpha() * t));

        painter.fillRect(rect, m_backgroundColor);
        painter.save();
        painter.translate(rect.center());
        painter.scale(scale, scale);
        painter.translate(-rect.center());
        painter.setFont(fontForCapture(capture));
        painter.setPen(color);
        painter.drawText(rect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextDontClip, text);
        painter.restore();
    }

    /* `character` is a direct byte offset (ASCII-only, as elsewhere).
     * offsetForLineColumn's clamping keeps a stale diagnostic from
     * drawing garbage. */
    for (const GuiDiagnostic &d : m_diagnostics) {
        size_t start = offsetForLineColumn(d.startLine, d.startChar);
        size_t end = offsetForLineColumn(d.endLine, d.endChar);
        if (end <= start) {
            end = start + 1;
        }
        drawDiagnosticUnderline(painter, start, end, firstLine, lastLine, colorForSeverity(d.severity));
    }
    painter.restore();

    /* Absolute widget space, at eased positions, so a caret mid-glide
     * isn't dragged by the instant scroll target.
     *
     * Both branches key off m_idleTicks, not a free-running counter, so
     * neither fades while the caret is being moved. cos, not sin, so
     * phase 0 is full brightness and the fade starts from solid. */
    int caretAlpha = 255;
    if (!hasFocus()) {
        /* The keyboard is somewhere else — a panel, a find bar. A caret
         * that goes on pulsing there claims keystrokes it will not
         * receive, so it stops and dims to a marker of where you were.
         * See docs/adr/0121. */
        caretAlpha = kUnfocusedCaretAlpha;
    } else if (m_animationsEnabled) {
        int cycle = caretAnimationTicks();
        double phase = (m_idleTicks % cycle) / static_cast<double>(cycle);
        caretAlpha = std::clamp(static_cast<int>(128 + 127 * std::cos(phase * kTwoPi)), 0, 255);
    } else if (!m_caretVisible) {
        caretAlpha = 0;
    }

    /* Normal and Visual get a block cursor; Insert keeps the bar. */
    bool blockCursor = vimModeActive() && m_vimMode != VimMode::Insert;

    if (caretAlpha > 0 && !m_renderedCaretPos.isEmpty()) {
        painter.save();
        painter.setClipRect(QRect(gutter, 0, textAreaWidth, height()));
        if (blockCursor) {
            /* Same alpha range as the bar caret; legibility is the
             * knockout's job, below. */
            QColor blockColor = m_textColor;
            blockColor.setAlpha(caretAlpha);
            for (int i = 0; i < m_renderedCaretPos.size(); ++i) {
                const QPointF &pos = m_renderedCaretPos[i];
                int width = m_charWidth;
                AseHighlightCapture capture = ASE_HL_NONE;
                QString glyph = vimBlockGlyphAt(m_cursors[i], &width, &capture);

                painter.fillRect(QRectF(pos.x(), pos.y() + kCaretVerticalInset, width,
                                         m_lineHeight - 2 * kCaretVerticalInset),
                                  blockColor);
                /* A threshold, not a ramp. Contrast works out as
                 * |1 - knockout - block|, so any continuous knockout
                 * ramp passes through zero somewhere; a step keeps it at
                 * half or better throughout. See docs/adr/0071. */
                if (!glyph.isEmpty() && caretAlpha >= 128) {
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
                painter.fillRect(QRectF(pos.x(), pos.y() + kCaretVerticalInset, kCaretWidth,
                                         m_lineHeight - 2 * kCaretVerticalInset),
                                  caretColor);
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

            /* A thin bar at the very edge, left of the diagnostic dot
             * and the numbers. It is the quietest mark that still reads
             * at a glance, which is what the gutter is for — see
             * docs/adr/0112. */
            AseVcsLineStatus vcs = vcsStatusForLine(line);
            if (vcs != ASE_VCS_UNCHANGED) {
                QColor bar = colorForVcsStatus(vcs);
                painter.save();
                painter.setPen(Qt::NoPen);
                painter.setBrush(bar);
                if (vcs == ASE_VCS_DELETED) {
                    /* Removed lines have no line of their own, so the
                     * mark sits at the boundary rather than spanning a
                     * line that is still there. */
                    painter.drawRect(QRectF(0.0, y + m_lineHeight - 2.0, kVcsBarWidth, 2.0));
                } else {
                    painter.drawRect(QRectF(0.0, y, kVcsBarWidth, m_lineHeight));
                }
                painter.restore();
            }

            /* In the left padding the right-aligned numbers never
             * reach. No wipe, but shares the underline's reveal. */
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

/* One easing step toward the logical target. See docs/adr/0015. */
void EditorViewport::updateAnimation() {
    updateDiagnosticLineHighlights();
    updateWelcomeOverlayOpacity();

    if (!m_animationsEnabled) {
        /* Snap, do NOT clear m_renderedCaretPos: paintEvent only draws
         * carets when it is non-empty. */
        snapAnimationToTarget();
        return;
    }

    double deltaLine = m_scrollLine - m_renderedScrollLine;
    m_renderedScrollLine += (std::abs(deltaLine) < 0.02) ? deltaLine : deltaLine * kEaseFactor;

    double deltaX = m_scrollX - m_renderedScrollX;
    m_renderedScrollX += (std::abs(deltaX) < 0.5) ? deltaX : deltaX * kEaseFactor;

    /* The easing above moved the viewport; the window must follow before
     * caretTargetFor() measures against it. */
    ensureCaptureWindowForViewport();

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

    /* Ages each entry, dropping elapsed ones and any whose range an
     * edit has since invalidated. */
    for (int i = m_typingAnimations.size() - 1; i >= 0; --i) {
        TypingAnimation &anim = m_typingAnimations[i];
        anim.elapsedTicks++;
        if (anim.elapsedTicks >= typingAnimationTicks() ||
            anim.start + anim.length > static_cast<size_t>(m_cache.size())) {
            m_typingAnimations.remove(i);
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
    /* As in updateAnimation(), but with no easing afterwards to correct
     * a bad measurement. */
    ensureCaptureWindowForViewport();
    m_renderedCaretPos.resize(m_cursors.size());
    for (int i = 0; i < m_cursors.size(); ++i) {
        m_renderedCaretPos[i] = caretTargetFor(m_cursors[i]);
    }
    /* Does NOT clear m_typingAnimations: insertText() calls this right
     * after pushing an entry, which would wipe it before its first
     * frame. Callers needing that clear it themselves. */
}

/* One entry per byte, so drawLine and xForColumn segment runs
 * identically. A slice of m_captureAt. See docs/adr/0053. */
QVector<AseHighlightCapture> EditorViewport::capturesForLine(int start, int end) const {
    int lineLen = std::max(0, end - start);
    QVector<AseHighlightCapture> captures(lineLen, ASE_HL_NONE);
    int limit = std::min(end, static_cast<int>(m_captureAt.size()));
    for (int i = start; i < limit; ++i) {
        captures[i - start] = static_cast<AseHighlightCapture>(m_captureAt[i]);
    }
    return captures;
}

/* Splits [start, end) into same-capture runs, each drawn with its own
 * style. Advances by each run's measured width, not runLen *
 * m_charWidth, which drifts on long lines (docs/adr/0013).
 *
 * The rect width must be that measured width, never width() - x: under
 * horizontal scroll x is a translated coordinate that can exceed
 * width(), and a negative-width QRect draws nothing. */
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
        /* TextDontClip: m_lineHeight comes from the plain font, but
         * italics can report a taller ascent and got their ascenders
         * sheared ("void" read as "voia"). This rect is for positioning
         * only; paintEvent's clip already bounds the painting. */
        painter.drawText(QRect(x, y, runWidth, m_lineHeight), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextDontClip,
                          text);
        x += runWidth;
        runStart = i;
    }
}

/* drawLine's segmentation, stopped at `column`, so the caret lands
 * where the character was actually painted. See docs/adr/0013. */
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

/* Inverse of xForColumn, for hit-testing. Same per-run measurement —
 * the fixed-pitch assumption drifted the click column rightward along a
 * line (docs/adr/0039). Steps by codepoint inside the run so a
 * multi-byte character is never measured as a partial sequence, and
 * rounds to the nearest boundary rather than flooring. */
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

/* One rect per visual line [start, end) touches. */
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
        /* The end is exclusive, so a range finishing exactly at a
         * line's start covers none of that line. Painting it anyway hit
         * the max(1, ...) below and drew a one-pixel sliver down the
         * left edge of the row — which is what a linewise selection
         * ends at, every time. See docs/adr/0123. */
        if (rangeEndCol <= rangeStartCol) {
            continue;
        }
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

/* Dim by default, wiping to full opacity left-to-right across this
 * diagnostic's own span while the cursor is on the line. */
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
        /* Shifted down for clearance from descenders. */
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

/* Absolute widget coordinates: this is centred on the viewport, not on
 * the document. The wordmark already spells the name, so no text title
 * is drawn under it. See docs/adr/0042. */
void EditorViewport::drawWelcomeOverlay(QPainter &painter) const {
    if (m_welcomeOverlayOpacity <= 0.0) {
        return;
    }

    painter.save();
    painter.setOpacity(m_welcomeOverlayOpacity);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QPixmap logo(QStringLiteral(":/ase.png"));
    QPixmap scaledLogo;
    if (!logo.isNull()) {
        scaledLogo = logo.scaledToWidth(140, Qt::SmoothTransformation);
    }

    /* A deliberately short list of what you cannot discover by poking
     * around. See docs/adr/0057. */
    struct WelcomeInstruction {
        QString description;
        QString key;
    };
    static const QVector<WelcomeInstruction> kInstructions = {
        {QStringLiteral("All keyboard shortcuts"), QStringLiteral("F1")},
        {QStringLiteral("Start typing (Vim: Normal mode)"), QStringLiteral("i")},
        {QStringLiteral("New file"), QStringLiteral("Ctrl+N")},
        {QStringLiteral("Open a file"), QStringLiteral("Alt+O")},
        {QStringLiteral("Save"), QStringLiteral("Ctrl+S")},
        {QStringLiteral("Command line"), QStringLiteral(":")},
    };

    QFontMetrics metrics(m_font);
    QFont titleFont = m_font;
    titleFont.setBold(true);
    QFontMetrics titleMetrics(titleFont);

    int lineSpacing = metrics.height() + 4;
    constexpr int kLogoGap = 18;
    constexpr int kIdentityGap = 6;
    constexpr int kBlockGap = 26;
    constexpr double kDotSize = 5.0;
    constexpr int kDotTextGap = 12;
    constexpr int kColumnGap = 32;

    const QString version = QStringLiteral("Version %1").arg(QStringLiteral(ASE_VERSION_STRING));
    const QString author = QStringLiteral("Developed by Saeed");
    const QString tagline = QStringLiteral("The absolutely simple text editor");

    int maxDescWidth = 0;
    int maxKeyWidth = 0;
    for (const WelcomeInstruction &instr : kInstructions) {
        maxDescWidth = std::max(maxDescWidth, metrics.horizontalAdvance(instr.description));
        maxKeyWidth = std::max(maxKeyWidth, metrics.horizontalAdvance(instr.key));
    }
    int rowWidth = static_cast<int>(kDotSize) + kDotTextGap + maxDescWidth + kColumnGap + maxKeyWidth;

    int identityHeight = lineSpacing * 2 + titleMetrics.height() + kIdentityGap * 2;
    int totalHeight = scaledLogo.height() + (scaledLogo.isNull() ? 0 : kLogoGap) + identityHeight + kBlockGap +
                       kInstructions.size() * lineSpacing;
    int top = (height() - totalHeight) / 2;

    if (!scaledLogo.isNull()) {
        painter.drawPixmap((width() - scaledLogo.width()) / 2, top, scaledLogo);
        top += scaledLogo.height() + kLogoGap;
    }

    /* Three tiers by opacity only; the tagline reads strongest. */
    QColor strong = m_textColor;
    strong.setAlpha(200);
    QColor dim = m_textColor;
    dim.setAlpha(120);
    QColor rowColor = m_textColor;
    rowColor.setAlpha(160);

    painter.setFont(m_font);
    painter.setPen(dim);
    painter.drawText(QRect(0, top, width(), lineSpacing), Qt::AlignHCenter | Qt::AlignVCenter, version);
    top += lineSpacing + kIdentityGap;

    painter.drawText(QRect(0, top, width(), lineSpacing), Qt::AlignHCenter | Qt::AlignVCenter, author);
    top += lineSpacing + kIdentityGap;

    painter.setFont(titleFont);
    painter.setPen(strong);
    painter.drawText(QRect(0, top, width(), titleMetrics.height()), Qt::AlignHCenter | Qt::AlignVCenter, tagline);
    top += titleMetrics.height() + kBlockGap;

    painter.setFont(m_font);
    int rowLeft = (width() - rowWidth) / 2;
    int descLeft = rowLeft + static_cast<int>(kDotSize) + kDotTextGap;
    int keyLeft = rowLeft + rowWidth - maxKeyWidth;
    for (const WelcomeInstruction &instr : kInstructions) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(rowColor);
        painter.drawEllipse(QRectF(rowLeft, top + (lineSpacing - kDotSize) / 2.0, kDotSize, kDotSize));

        painter.setPen(rowColor);
        painter.drawText(QRect(descLeft, top, maxDescWidth, lineSpacing), Qt::AlignLeft | Qt::AlignVCenter,
                          instr.description);
        painter.drawText(QRect(keyLeft, top, maxKeyWidth, lineSpacing), Qt::AlignRight | Qt::AlignVCenter,
                          instr.key);
        top += lineSpacing;
    }

    painter.restore();
}

int EditorViewport::gutterWidth() const {
    if (m_lineNumberMode == QLatin1String("off")) {
        return 0;
    }
    /* Measured from the widest actual label, not digits * a fixed
     * width. */
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
    }
    return font;
}

const QFontMetrics &EditorViewport::metricsForCapture(AseHighlightCapture capture) const {
    if (capture == ASE_HL_KEYWORD) {
        return m_boldMetrics;
    }
    return m_metrics;
}

QColor EditorViewport::colorForCapture(AseHighlightCapture capture) const {
    switch (capture) {
    case ASE_HL_TYPE:
        /* One of the two captures with a real hue. See docs/adr/0048. */
        return m_syntaxTypeColor;
    case ASE_HL_STRING:
        return m_syntaxStringColor;
    case ASE_HL_NUMBER: {
        QColor color = m_textColor;
        color.setAlpha(200);
        return color;
    }
    case ASE_HL_COMMENT: {
        /* 145, not 115: the latter measured 3.64:1, below WCAG AA. */
        QColor color = m_textColor;
        color.setAlpha(145);
        return color;
    }
    case ASE_HL_KEYWORD:
    case ASE_HL_NONE:
    default:
        return m_textColor;
    }
}

/* Severity colour-coding is too expected a convention to fold into
 * opacity. Information/Hint fall back to dim text. */
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

/* Every frame; with animations off it snaps rather than eases. */
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
    /* One-way latch: the moment there is any content, the greeting is
     * done for this buffer's lifetime. */
    if (!m_cache.isEmpty()) {
        m_welcomeEligible = false;
    }
    double target = (m_welcomeEligible && m_cache.isEmpty()) ? 1.0 : 0.0;
    double delta = target - m_welcomeOverlayOpacity;
    if (!m_animationsEnabled || std::abs(delta) < kOpacitySnapThreshold) {
        m_welcomeOverlayOpacity = target;
    } else {
        m_welcomeOverlayOpacity += delta * kEaseFactor;
    }
}

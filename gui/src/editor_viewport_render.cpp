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
/* The caret breathe cycle, in ticks of motion::kTickMs — 24 * 30ms =
 * ~720ms. Was 34 (~1020ms); sped up along with every other animation in
 * the app, see docs/adr/0027. */
constexpr int kCaretAnimationTicks = 24;
constexpr double kTwoPi = 6.283185307179586;
/* Eased 0..1 progress -> scale, i.e. a typed character starts at 85%
 * size and grows to 100% as it fades in — paired with kEaseFactor-style
 * quick convergence (ease-out, not linear) for the same snappy feel
 * every other animation here has. See docs/adr/0049. */
constexpr double kTypingAnimationStartScale = 0.85;
/* Both caret shapes are inset from the full line height by this much on
 * top and bottom — a full-line-height caret reads as slightly too tall/
 * blocky against the actual glyph height. The glyph knocked out on top
 * of the block cursor still uses the *un-inset* rect so its
 * baseline/vertical centering stays identical to normal text. */
constexpr int kCaretVerticalInset = 2;
constexpr int kGutterPadding = 8; /* on each side of the line-number text */
/* The app's frame-driven easing, and the tick it advances on — both
 * live in motion.h now so the viewport's own motion is declared
 * alongside the chrome tiers rather than separately from them. See
 * docs/adr/0053. */
using motion::kEaseFactor;

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

    /* Typing pop-in — see docs/adr/0049. drawLine above already painted
     * these bytes at full size/opacity; re-paint over just that rect
     * (background fill, then the scaled/faded glyph on top) rather than
     * teaching drawLine's run segmentation about a third, transient
     * state. Only ever non-empty when animations are enabled. */
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

        double progress = std::clamp(static_cast<double>(anim.elapsedTicks) / kTypingAnimationTicks, 0.0, 1.0);
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

    /* Normal and Visual mode both get a real vim-style block cursor —
     * filling the whole character cell, not the thin insertion bar used
     * in Insert — since otherwise this editor's caret looks identical
     * regardless of mode, with no visual cue that every keystroke
     * currently means something completely different. Insert keeps the
     * bar (it still means "an insertion point," same as with Vim mode
     * off). See docs/adr/0047, docs/adr/0051. */
    bool blockCursor = vimModeActive() && m_vimMode != VimMode::Insert;

    if (caretAlpha > 0 && !m_renderedCaretPos.isEmpty()) {
        painter.save();
        painter.setClipRect(QRect(gutter, 0, textAreaWidth, height()));
        if (blockCursor) {
            /* Same alpha range as the bar caret — a capped, never-fully-
             * opaque block was tried (ADR 0047) and reverted per direct
             * feedback: it read as wrong, not as a considered choice.
             * The knocked-out glyph drawn on top already guarantees
             * legibility regardless of the block's opacity. */
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

    /* Typing pop-in — see docs/adr/0049. Ages every entry by one tick and
     * drops it once its animation window has elapsed, or defensively if
     * an edit elsewhere has since made its byte range invalid (out of
     * bounds) — paintEvent's own bounds check covers the "still in
     * bounds but now different content" case by construction (it always
     * reads m_cache fresh), this one just prevents the list from
     * growing forever. */
    for (int i = m_typingAnimations.size() - 1; i >= 0; --i) {
        TypingAnimation &anim = m_typingAnimations[i];
        anim.elapsedTicks++;
        if (anim.elapsedTicks >= kTypingAnimationTicks ||
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
    m_renderedCaretPos.resize(m_cursors.size());
    for (int i = 0; i < m_cursors.size(); ++i) {
        m_renderedCaretPos[i] = caretTargetFor(m_cursors[i]);
    }
    /* Does NOT clear m_typingAnimations — insertText() (the only place
     * that populates it) calls this same function right after pushing a
     * fresh entry (via pasteClipboard's own snap call further down this
     * file); clearing here would wipe that entry before it ever
     * rendered a single frame. Call sites that genuinely need to
     * invalidate an in-flight pop-in (undo/redo restoring arbitrary
     * content) clear it explicitly themselves instead — see
     * applyUndoResult(). */
}

/* One entry per byte in [start, end), naming which capture (if any) that
 * byte belongs to — shared by drawLine and xForColumn so both segment a
 * line into runs identically.
 *
 * A straight slice of m_captureAt, which refreshCache() already
 * flattened once per edit. This used to re-derive the answer by
 * scanning every span in the file, per line, per frame — see the
 * comment on m_captureAt's fill loop and docs/adr/0053. */
QVector<AseHighlightCapture> EditorViewport::capturesForLine(int start, int end) const {
    int lineLen = std::max(0, end - start);
    QVector<AseHighlightCapture> captures(lineLen, ASE_HL_NONE);
    int limit = std::min(end, static_cast<int>(m_captureAt.size()));
    for (int i = start; i < limit; ++i) {
        captures[i - start] = static_cast<AseHighlightCapture>(m_captureAt[i]);
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
    painter.setRenderHint(QPainter::Antialiasing, true);

    QPixmap logo(QStringLiteral(":/ase.png"));
    QPixmap scaledLogo;
    if (!logo.isNull()) {
        scaledLogo = logo.scaledToWidth(140, Qt::SmoothTransformation);
    }

    /* Identity block (centered) above, shortcuts (two aligned columns)
     * below. The shortcuts are a deliberately short list of what you
     * cannot discover by poking around — F1 is first because it is
     * the answer to every other question, and the rest are the things
     * you need before you can do anything at all: make a file, open
     * one, save it, and (since Vim mode is on by default now) the fact
     * that you start in Normal mode and `i` is how you type. See
     * docs/adr/0057. */
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

    /* Three tiers of emphasis, by opacity only (docs/adr/0007): the
     * tagline is what the editor *is*, so it reads strongest; version
     * and byline are context. */
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
        /* The one deliberate hue, alongside ASE_HL_STRING below — see
         * docs/adr/0048 for why only these two captures get a real
         * color instead of the monochrome dim/bold treatment
         * everything else here still uses. */
        return m_syntaxTypeColor;
    case ASE_HL_STRING:
        return m_syntaxStringColor;
    case ASE_HL_NUMBER: {
        QColor color = m_textColor;
        color.setAlpha(200);
        return color;
    }
    case ASE_HL_COMMENT: {
        /* 145/255 (~57%), not the original 115/255 (~45%) — that measured
         * 3.64:1 against the background, below WCAG AA's 4.5:1 for normal
         * text. See docs/adr/0012, decision 4. */
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

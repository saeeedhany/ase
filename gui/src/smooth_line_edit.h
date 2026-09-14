#ifndef ASE_SMOOTH_LINE_EDIT_H
#define ASE_SMOOTH_LINE_EDIT_H

#include <QColor>
#include <QLineEdit>

class QTimer;
class EditorViewport;

/*
 * A QLineEdit whose caret glides and breathes exactly like the editor's
 * own — see docs/adr/0058.
 *
 * Every text field in this app's panels (Find/Replace, Open/Save-As,
 * the `:` command line, the shortcut search) used a plain QLineEdit, so
 * the caret you were looking at changed character the moment you opened
 * a panel: the editor's caret glides between columns and fades on a
 * cosine breathe cycle, while Qt's native caret hard-blinks in place.
 * ADR 0028 narrowed the gap as far as it could without custom painting,
 * by matching the *rate* through QApplication::setCursorFlashTime, and
 * explicitly left the rest for when the rate match alone stopped being
 * enough. It did.
 *
 * Two pieces make this work, both worth knowing before touching it:
 *
 *  - **The native caret is suppressed through the style**, not hidden by
 *    painting over it. QLineEdit takes its caret width from
 *    QStyle::PM_TextCursorWidth, so a QProxyStyle reporting 0 for that
 *    one metric removes it while leaving text, selection, placeholder
 *    and frame painting completely untouched. There is no public
 *    setCursorWidth() on QLineEdit (QPlainTextEdit has one; QLineEdit
 *    does not), and painting a background patch over the native caret
 *    would mean re-rendering the text under it.
 *
 *  - **The caret position comes from QLineEdit::cursorRect()**, so
 *    horizontal scrolling in an overflowing field, margins and
 *    alignment are all Qt's problem rather than ours. That rect is
 *    deliberately padded for repaint purposes — the caret sits at
 *    ceil((width - cursorWidth) / 2) from its left edge, derived from
 *    the rect rather than hardcoded so it survives a change to that
 *    padding.
 */
class SmoothLineEdit : public QLineEdit {
    Q_OBJECT

public:
    explicit SmoothLineEdit(QWidget *parent = nullptr);

    /* Follows EditorViewport::animationsEnabled(), pushed in by each
     * panel's refreshTheme() alongside the colours — so `animations =
     * false` in config.ase turns the glide and fade off here too, and a
     * hot-reload reaches it. With animations off the caret hard-blinks
     * on the same cadence the editor uses in that mode. */
    void setAnimated(bool animated);

    /* Everything a panel's text field needs to look like this app's
     * text fields: the field tone, full-contrast text, a selection that
     * matches the panel's own border rather than the platform's blue,
     * the placeholder one opacity tier down (Qt derives it from Text
     * otherwise, and that derivation lands nearly black on a dark
     * theme), and the caret animation following `animations`.
     *
     * Four panels were each doing this by hand in five or six lines,
     * with the placeholder fix present in two of them and missing from
     * the rest — which is what a copied idiom always eventually looks
     * like. See docs/adr/0072. */
    void applyPanelTheme(const EditorViewport *viewport);

private:
    void tick();
    double caretTargetX() const;

    QTimer *m_timer;
    /* Interpolated caret x, in widget coordinates. Negative means "not
     * placed yet" — the next frame snaps rather than gliding in from the
     * left edge. */
    double m_caretX = -1.0;
    /* Ticks since the last caret-moving action, reset by every edit and
     * cursor move. Drives the breathe phase and the hard blink both, the
     * same single counter the editor uses (docs/adr/0017). */
    int m_idleTicks = 0;
    bool m_caretVisible = true;
    bool m_animated = true;

protected:
    void paintEvent(QPaintEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void showEvent(QShowEvent *event) override;
};

#endif /* ASE_SMOOTH_LINE_EDIT_H */

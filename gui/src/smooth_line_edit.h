#ifndef ASE_SMOOTH_LINE_EDIT_H
#define ASE_SMOOTH_LINE_EDIT_H

#include <QColor>
#include <QLineEdit>

class QTimer;
class EditorViewport;

/*
 * A QLineEdit whose caret glides and breathes like the editor's own.
 * See docs/adr/0058.
 *
 * Two things to know before touching it:
 *
 *  - The native caret is suppressed through the style, not painted
 *    over: a QProxyStyle reporting 0 for PM_TextCursorWidth removes it
 *    while leaving text, selection and frame painting alone. QLineEdit
 *    has no setCursorWidth(), and painting over the caret would mean
 *    re-rendering the text beneath it.
 *
 *  - The position comes from QLineEdit::cursorRect(), so scrolling,
 *    margins and alignment stay Qt's problem. That rect is padded, so
 *    the caret sits at ceil((width - cursorWidth) / 2) from its left
 *    edge, derived rather than hardcoded.
 */
class SmoothLineEdit : public QLineEdit {
    Q_OBJECT

public:
    explicit SmoothLineEdit(QWidget *parent = nullptr);

    /* Pushed in by each panel's refreshTheme(), so a hot-reloaded
     * `animations = false` reaches this caret too. */
    void setAnimated(bool animated);

    /* Everything a panel's field needs: field tone, full-contrast
     * text, a selection matching the panel border, and the placeholder
     * a tier down — Qt's own derivation lands nearly black on a dark
     * theme. Four panels each did this by hand, two with the
     * placeholder fix and two without. */
    void applyPanelTheme(const EditorViewport *viewport);

private:
    void tick();
    double caretTargetX() const;

    QTimer *m_timer;
    /* Negative means not placed yet: the next frame snaps. */
    double m_caretX = -1.0;
    /* Drives both the breathe phase and the hard blink. */
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

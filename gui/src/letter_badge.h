#ifndef ASE_LETTER_BADGE_H
#define ASE_LETTER_BADGE_H

#include <QColor>
#include <QWidget>

/*
 * A small flat square carrying one bold letter — this app's compact
 * stand-in for a text label on its floating panels (see docs/adr/0022):
 * "F"/"R" on the find/replace panel today, "O"/"S" on future Open/
 * Save-As panels. Colors are always set explicitly by the owning panel
 * (no config access of its own — same reasoning as FloatingPanel), so
 * the letter renders at full contrast regardless of theme.
 */
class LetterBadge : public QWidget {
public:
    explicit LetterBadge(QChar letter, QWidget *parent = nullptr);

    void setColors(const QColor &fill, const QColor &letterColor);
    /* Lets one badge instance switch label — e.g. a single Open/Save-As
     * panel reusing one badge for "O" vs "S" instead of two badges
     * shown/hidden by mode. */
    void setLetter(QChar letter);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QChar m_letter;
    QColor m_fill;
    QColor m_letterColor;
};

#endif /* ASE_LETTER_BADGE_H */

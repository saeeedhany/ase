#ifndef ASE_TRANSLUCENT_BAR_H
#define ASE_TRANSLUCENT_BAR_H

#include <QColor>
#include <QWidget>

/* A plain rect filled via QPainter::fillRect, not QPalette/
 * setAutoFillBackground — the latter is a real trap for a translucent
 * QColor: it silently ignores the alpha channel and paints fully
 * opaque. Originally written for FileBrowserPanel's sliding row
 * highlight (docs/adr/0024), where that exact bug hid a whole list row
 * under an accidentally-opaque bar; reused since for the small seam
 * marker between the editor and the output panel (docs/adr/0027). */
class TranslucentBar : public QWidget {
public:
    explicit TranslucentBar(QWidget *parent);
    void setColor(const QColor &color);
    /* 0 (default) keeps the hard rectangle the seam marker wants; the
     * file browser's row highlight uses a small radius so a bar sliding
     * between rows reads as a soft highlight rather than a block. */
    void setRadius(double radius);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QColor m_color;
    double m_radius = 0.0;
};

#endif /* ASE_TRANSLUCENT_BAR_H */

#ifndef ASE_BUFFER_BAR_H
#define ASE_BUFFER_BAR_H

#include <QColor>
#include <QtGlobal>
#include <QFont>
#include <QRect>
#include <QString>
#include <QVector>
#include <QWidget>

class QVariantAnimation;
class QWheelEvent;

/*
 * The open-buffer tab strip. Deliberately not a QTabBar: native tab
 * widgets draw frames, separators and hover plates this app has
 * nowhere else. A tab is a filename, a close mark, and a dot when
 * unsaved.
 *
 * Tab slots are a fixed width whichever is active, so switching never
 * reflows the strip, and every layout change interpolates from where
 * things were. See docs/adr/0054, docs/adr/0056.
 */
class BufferBar : public QWidget {
    Q_OBJECT

public:
    explicit BufferBar(QWidget *parent = nullptr);

    struct Item {
        /* Matching by name broke as soon as two tabs shared one, which
         * every "untitled" buffer does: a new one matched an older and
         * never animated in. */
        quintptr id = 0;
        QString name;
        bool dirty = false;
    };

    void setEntries(const QVector<Item> &items, int activeIndex);

    /* From the editor's theme, re-applied on hot-reload. The font
     * comes from the editor so tabs match the text they label. */
    void setColors(const QColor &background, const QColor &text);
    void setBaseFont(const QFont &font);

signals:
    void bufferSelected(int index);
    void bufferCloseRequested(int index);
    void newBufferRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    QSize sizeHint() const override;

private:
    /* Where a tab belongs once settled; rendering interpolates from
     * `fromX` by m_transition. */
    struct Tab {
        quintptr id = 0;
        QString name;
        bool dirty = false;
        double x = 0.0;     /* settled left edge */
        double fromX = 0.0; /* left edge it is animating out of */
        /* Real 0..255 alpha, so a new tab can start fully invisible. */
        double fromAlpha = 255.0;
        double width = 0.0;
        /* Closed but still on screen, collapsing into the tab it
         * leaves focus to. Never hit-testable. */
        bool closing = false;
    };

    void relayout(bool animate, int previousActiveIndex);
    void clampScroll();
    double contentWidth() const;
    int tabAt(const QPoint &pos) const;
    QRect closeRectFor(int index) const;
    QRect plusRect() const;
    double tabLeft(int index) const;
    int barHeight() const;

    QVector<Item> m_items;
    QVector<Tab> m_tabs;
    int m_activeIndex = -1;
    int m_hoverIndex = -1;
    bool m_hoverClose = false;
    bool m_hoverPlus = false;

    QVariantAnimation *m_animation = nullptr;
    double m_transition = 1.0; /* 0 = fully in the "from" layout, 1 = settled */
    /* Horizontal offset for when there are more tabs than fit — see
     * wheelEvent. */
    double m_scrollX = 0.0;

    QColor m_background;
    QColor m_text;
    QFont m_baseFont;
};

#endif /* ASE_BUFFER_BAR_H */

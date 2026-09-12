#ifndef ASE_BUFFER_BAR_H
#define ASE_BUFFER_BAR_H

#include <QColor>
#include <QFont>
#include <QRect>
#include <QString>
#include <QVector>
#include <QWidget>

class QVariantAnimation;
class QWheelEvent;

/*
 * The open-buffer tab strip along the top of the window — see
 * docs/adr/0054 and docs/adr/0056.
 *
 * Deliberately not a QTabBar. Every native tab widget draws frames,
 * separators, a selected-tab lip and a hover plate, none of which this
 * app has anywhere else — the chrome language here is "no lines, state
 * carried by opacity" (docs/adr/0007's one-font-color pillar,
 * docs/adr/0022's panels). A tab is a filename, a close mark, and a dot
 * only when that buffer has unsaved changes.
 *
 * Everything that can move, moves smoothly. Tab slots are a *fixed*
 * width regardless of which one is active (the close mark fades in
 * rather than taking space), so switching tabs never reflows the strip;
 * and every layout change — opening, closing, switching — is
 * interpolated from where things were to where they now belong, so a new
 * tab visibly slides out of the one it was opened from instead of
 * appearing fully formed. See docs/adr/0056 for why the earlier
 * version felt jumpy.
 */
class BufferBar : public QWidget {
    Q_OBJECT

public:
    explicit BufferBar(QWidget *parent = nullptr);

    struct Item {
        QString name;
        bool dirty = false;
    };

    void setEntries(const QVector<Item> &items, int activeIndex);

    /* Pulled from EditorViewport's config-driven theme, like every other
     * piece of chrome — re-applied on config hot-reload so an edited
     * config.ase reaches this too (docs/adr/0024). The font comes from
     * the editor so tabs sit at the same visual weight as the text they
     * label, and follow Ctrl+=/Ctrl+- with it. */
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
    /* Where a tab belongs once everything has settled. Rendering
     * interpolates between `fromX` and `x` by m_transition, so the same
     * mechanism covers opening, closing and switching. */
    struct Tab {
        QString name;
        bool dirty = false;
        double x = 0.0;     /* settled left edge */
        double fromX = 0.0; /* left edge it is animating out of */
        double fromAlpha = 1.0;
        double width = 0.0;
        /* A tab that has been closed but is still on screen, collapsing
         * back into the one it leaves focus to — the exact reverse of
         * how it arrived. Dropped once the animation lands; never
         * hit-testable meanwhile. */
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

#ifndef ASE_BUFFER_BAR_H
#define ASE_BUFFER_BAR_H

#include <QColor>
#include <QRect>
#include <QString>
#include <QVector>
#include <QWidget>

/*
 * The open-buffer strip along the top of the window — see docs/adr/0054.
 *
 * Deliberately not a QTabBar. Every native tab widget draws frames,
 * separators, a selected-tab lip and a hover plate, none of which this
 * app has anywhere else — the whole chrome language here is "no lines,
 * state carried by opacity" (docs/adr/0007's one-font-color pillar,
 * docs/adr/0022's panels). So an entry is exactly a dot, a filename, and
 * — only while it is the active one — a close mark. Nothing is boxed,
 * underlined or separated; which buffer you are in is told by opacity
 * alone, the same way the gutter already distinguishes the cursor's line
 * from the rest.
 */
class BufferBar : public QWidget {
    Q_OBJECT

public:
    explicit BufferBar(QWidget *parent = nullptr);

    struct Item {
        QString name;
        bool dirty = false;
    };

    /* Replaces the whole list — cheap enough at this scale (a handful of
     * entries, repainted only when the set, the dirty flags or the
     * active index actually change) that incremental updating would be
     * complexity for nothing. */
    void setEntries(const QVector<Item> &items, int activeIndex);

    /* Pulled from EditorViewport's config-driven theme, like every other
     * piece of chrome — re-applied on config hot-reload so an edited
     * config.ase reaches this too (docs/adr/0024). */
    void setColors(const QColor &background, const QColor &text);

signals:
    void bufferSelected(int index);
    void bufferCloseRequested(int index);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    QSize sizeHint() const override;

private:
    /* Geometry of one laid-out entry. Recomputed in layoutEntries() and
     * reused by both paint and hit-testing, so what is drawn and what is
     * clickable can never drift apart. */
    struct Entry {
        QString name;
        bool dirty = false;
        QRect bounds; /* whole clickable entry */
        QRect close;  /* the close mark; null unless this entry is active */
    };

    void layoutEntries();
    int entryAt(const QPoint &pos) const;

    QVector<Item> m_items;
    QVector<Entry> m_entries;
    int m_activeIndex = -1;
    int m_hoverIndex = -1;
    bool m_hoverClose = false;

    QColor m_background;
    QColor m_text;
};

#endif /* ASE_BUFFER_BAR_H */

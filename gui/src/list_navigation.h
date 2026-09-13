#ifndef ASE_LIST_NAVIGATION_H
#define ASE_LIST_NAVIGATION_H

#include <QKeyEvent>

/*
 * "Move down / move up a list", in one place — see docs/adr/0069.
 *
 * Every list in this app is something you drive from a text field you
 * are still typing into: the completion popup while you type an
 * identifier, the file browser while you type a filter, quick open
 * while you type a query. Reaching for the arrow keys means leaving the
 * home row in the middle of a word, which is exactly what `Ctrl+J` /
 * `Ctrl+K` exist to avoid — and they are the same j/k this editor's
 * Normal mode already trains your fingers on.
 *
 * The arrows keep working everywhere they did. This adds a way, it does
 * not replace one.
 *
 * A shared helper rather than four copies of the same two-key check,
 * for the reason docs/adr/0053 gives about animation durations: a rule
 * re-implemented per call site is a rule that drifts. A list that
 * answered `Ctrl+J` in three panels and not the fourth would be worse
 * than one that never did.
 */
namespace listnav {

/* +1 to move down, -1 to move up, 0 if this key means neither. */
inline int delta(const QKeyEvent *event) {
    if (event->modifiers() & Qt::ControlModifier) {
        if (event->key() == Qt::Key_J) {
            return 1;
        }
        if (event->key() == Qt::Key_K) {
            return -1;
        }
        return 0;
    }
    if (event->key() == Qt::Key_Down) {
        return 1;
    }
    if (event->key() == Qt::Key_Up) {
        return -1;
    }
    return 0;
}

} // namespace listnav

#endif /* ASE_LIST_NAVIGATION_H */

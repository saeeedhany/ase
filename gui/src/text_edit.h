#ifndef ASE_TEXT_EDIT_H
#define ASE_TEXT_EDIT_H

#include <QByteArray>

/*
 * One replacement, named the way a search hit names it: 1-based line
 * and byte column, and how many bytes it stands for.
 *
 * Its own header because both ends of a multi-file edit need it — the
 * viewport that applies a file's share, and the layer that turns hits
 * into shares — and neither should have to include the other. See
 * docs/adr/0131.
 */
struct TextEdit {
    int line;
    int column;
    int length;
    QByteArray replacement;
};

#endif /* ASE_TEXT_EDIT_H */

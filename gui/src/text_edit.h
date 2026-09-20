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
    /* What must already be there, when the producer knows. An edit
     * whose bytes do not match is skipped.
     *
     * It is what makes an edit checkable rather than trusted: a file
     * that changed between the search and the apply, and an LSP range
     * measured in UTF-16 units against a line this treats as bytes,
     * both land here rather than corrupting the file. See
     * docs/adr/0131. */
    QByteArray expected;
};

#endif /* ASE_TEXT_EDIT_H */

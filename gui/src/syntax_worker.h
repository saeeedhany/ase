#ifndef ASE_SYNTAX_WORKER_H
#define ASE_SYNTAX_WORKER_H

#include <QByteArray>
#include <QObject>
#include <QVector>

extern "C" {
#include "ase/syntax.h"
}

/*
 * Parsing off the UI thread.
 *
 * Tree-sitter has to parse the whole file to build a tree, however
 * little of it is on screen — 230ms at 1MB, 1.07s at 4.5MB — and doing
 * that on the UI thread is what ADR 0107 answered by not doing it at
 * all past a size cap. A large file opened with no colours rather than
 * freezing first, which is the better of two bad answers.
 *
 * This is the third answer: the same parse, on a thread that is not
 * drawing anything. See docs/adr/0135.
 *
 * Ownership is the whole safety argument. This object owns its AseSyntax
 * and nothing else ever touches it; the text arrives as a QByteArray,
 * which is implicitly shared, so handing one over copies a pointer
 * rather than a megabyte and neither side mutates what the other holds.
 */
class SyntaxWorker : public QObject {
    Q_OBJECT

public:
    /* Created on the thread that will run it, not on the caller's — see
     * EditorViewport::startSyntaxWorker(). */
    explicit SyntaxWorker(AseLanguage language, QObject *parent = nullptr);
    ~SyntaxWorker() override;

public slots:
    /* `version` is handed back untouched. The caller decides whether the
     * answer still describes the buffer it asked about; by the time a
     * megabyte has been parsed it may not. */
    void parse(const QByteArray &text, quint64 version, int windowStart, int windowEnd);

signals:
    void parsed(const QVector<AseHighlightSpan> &spans, quint64 version, int windowStart,
                int windowEnd);

private:
    AseSyntax *m_syntax = nullptr;
    AseLanguage m_language;
    bool m_created = false;
};

#endif /* ASE_SYNTAX_WORKER_H */

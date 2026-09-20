#include "syntax_worker.h"

namespace {

void collectSpan(void *user_data, AseHighlightSpan span) {
    static_cast<QVector<AseHighlightSpan> *>(user_data)->push_back(span);
}

} // namespace

SyntaxWorker::SyntaxWorker(AseLanguage language, QObject *parent)
    : QObject(parent), m_language(language) {}

SyntaxWorker::~SyntaxWorker() {
    ase_syntax_destroy(m_syntax);
}

void SyntaxWorker::parse(const QByteArray &text, quint64 version, int windowStart, int windowEnd) {
    /* Built on first use, which is on this thread. Tree-sitter's parser
     * is fine used from one thread and not fine shared between two, so
     * it is never created anywhere but here. */
    if (!m_created) {
        m_syntax = ase_syntax_create(m_language);
        m_created = true;
    }
    if (m_syntax == nullptr) {
        emit parsed(QVector<AseHighlightSpan>(), version, windowStart, windowEnd);
        return;
    }

    QVector<AseHighlightSpan> spans;
    ase_syntax_highlight_range(m_syntax, text.constData(), static_cast<size_t>(text.size()),
                                static_cast<size_t>(windowStart), static_cast<size_t>(windowEnd),
                                collectSpan, &spans);
    emit parsed(spans, version, windowStart, windowEnd);
}

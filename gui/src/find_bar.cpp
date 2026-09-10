#include "find_bar.h"

#include "editor_viewport.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

FindBar::FindBar(EditorViewport *viewport, QWidget *parent) : QWidget(parent), m_viewport(viewport) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 4);
    layout->setSpacing(2);

    auto *findRow = new QHBoxLayout();
    findRow->addWidget(new QLabel(QStringLiteral("Find:"), this));
    m_findEdit = new QLineEdit(this);
    findRow->addWidget(m_findEdit);
    layout->addLayout(findRow);

    m_replaceRow = new QWidget(this);
    auto *replaceRowLayout = new QHBoxLayout(m_replaceRow);
    replaceRowLayout->setContentsMargins(0, 0, 0, 0);
    replaceRowLayout->addWidget(new QLabel(QStringLiteral("Replace:"), m_replaceRow));
    m_replaceEdit = new QLineEdit(m_replaceRow);
    replaceRowLayout->addWidget(m_replaceEdit);
    layout->addWidget(m_replaceRow);

    m_findEdit->installEventFilter(this);
    m_replaceEdit->installEventFilter(this);

    connect(m_findEdit, &QLineEdit::textChanged, this,
            [this](const QString &text) { m_viewport->setFindQuery(text); });

    setVisible(false);
    m_replaceRow->setVisible(false);
}

void FindBar::openFor(Mode mode) {
    bool replaceMode = (mode == Mode::Replace);
    m_replaceRow->setVisible(replaceMode);

    /* Pre-fill from the current selection, single-line only — a
     * multi-line "needle" as a starting point is more surprising than
     * helpful. */
    QString selection = m_viewport->primarySelectionText();
    if (!selection.isEmpty() && !selection.contains(QLatin1Char('\n'))) {
        m_findEdit->setText(selection);
    }

    setVisible(true);
    m_findEdit->setFocus();
    m_findEdit->selectAll();
    m_viewport->setFindQuery(m_findEdit->text());
}

void FindBar::hideBar() {
    setVisible(false);
    m_viewport->clearFindQuery();
    m_viewport->setFocus();
}

/* Return/Enter in the find field navigates (Shift = previous, wrapping
 * either direction); in the replace field it replaces the current match,
 * or all matches with Ctrl held. Escape closes the bar from either
 * field. No buttons in v1 — see docs/adr/0021. */
bool FindBar::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() != QEvent::KeyPress) {
        return QWidget::eventFilter(watched, event);
    }
    auto *keyEvent = static_cast<QKeyEvent *>(event);

    if (keyEvent->key() == Qt::Key_Escape) {
        hideBar();
        return true;
    }

    bool isReturn = keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter;
    if (!isReturn) {
        return QWidget::eventFilter(watched, event);
    }

    if (watched == m_findEdit) {
        if (keyEvent->modifiers() & Qt::ShiftModifier) {
            m_viewport->findPrevious();
        } else {
            m_viewport->findNext();
        }
        return true;
    }

    if (watched == m_replaceEdit) {
        if (keyEvent->modifiers() & Qt::ControlModifier) {
            m_viewport->replaceAllMatches(m_replaceEdit->text().toUtf8());
        } else {
            m_viewport->replaceCurrentMatch(m_replaceEdit->text().toUtf8());
        }
        return true;
    }

    return QWidget::eventFilter(watched, event);
}

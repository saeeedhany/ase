#include "smooth_scroll.h"

#include "editor_viewport.h"

#include <algorithm>

#include <QAbstractScrollArea>
#include <QApplication>
#include <QEasingCurve>
#include <QEvent>
#include <QPropertyAnimation>

#include "motion.h"
#include <QScrollBar>
#include <QWheelEvent>

namespace {
/* Installed on the scroll area's viewport (where Qt actually delivers
 * wheel events, not the QAbstractScrollArea itself — same place
 * QAbstractScrollAreaPrivate installs its own internal filter), so this
 * runs *before* the widget's native instant-jump handling and can
 * consume the event to replace it outright. Parented to the viewport it
 * watches, so it's destroyed automatically with it — nothing for a
 * caller to own or clean up. */
class SmoothScrollFilter : public QObject {
public:
    SmoothScrollFilter(QAbstractScrollArea *area, EditorViewport *viewport)
        : QObject(area->viewport()), m_area(area), m_viewport(viewport) {
        m_animation = new QPropertyAnimation(area->verticalScrollBar(), "value", this);
        motion::apply(m_animation, motion::kScroll);
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (event->type() != QEvent::Wheel || m_viewport == nullptr || !m_viewport->animationsEnabled()) {
            return QObject::eventFilter(watched, event);
        }

        QScrollBar *bar = m_area->verticalScrollBar();
        if (bar == nullptr || bar->maximum() == bar->minimum()) {
            return QObject::eventFilter(watched, event);
        }

        auto *wheelEvent = static_cast<QWheelEvent *>(event);
        /* Approximates the same magnitude Qt's own default wheel
         * handling would apply to this scrollbar (QApplication::
         * wheelScrollLines() "lines" of singleStep() each, per 120-
         * unit notch) — only *how* it gets there changes now (eased,
         * not instant). */
        int steps = wheelEvent->angleDelta().y() / 120;
        int delta = -steps * bar->singleStep() * qApp->wheelScrollLines();

        bool running = m_animation->state() == QAbstractAnimation::Running;
        int base = running ? m_targetValue : bar->value();
        m_targetValue = std::clamp(base + delta, bar->minimum(), bar->maximum());

        m_animation->stop();
        m_animation->setStartValue(bar->value());
        m_animation->setEndValue(m_targetValue);
        m_animation->start();
        return true; /* consumed — the native instant jump never runs */
    }

private:
    QAbstractScrollArea *m_area;
    EditorViewport *m_viewport;
    QPropertyAnimation *m_animation;
    int m_targetValue = 0;
};
} // namespace

void installSmoothScroll(QAbstractScrollArea *area, EditorViewport *viewport) {
    if (area == nullptr || area->viewport() == nullptr) {
        return;
    }
    area->viewport()->installEventFilter(new SmoothScrollFilter(area, viewport));
}

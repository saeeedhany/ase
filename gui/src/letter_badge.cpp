#include "letter_badge.h"

#include <QFont>
#include <QPainter>

namespace {
constexpr int kBadgeSize = 22;
}

LetterBadge::LetterBadge(QChar letter, QWidget *parent) : QWidget(parent), m_letter(letter) {
    setFixedSize(kBadgeSize, kBadgeSize);
}

void LetterBadge::setColors(const QColor &fill, const QColor &letterColor) {
    m_fill = fill;
    m_letterColor = letterColor;
    update();
}

QSize LetterBadge::sizeHint() const {
    return QSize(kBadgeSize, kBadgeSize);
}

void LetterBadge::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false); /* flat, crisp edges — see docs/adr/0022 */
    painter.fillRect(rect(), m_fill);

    QFont font = painter.font();
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(m_letterColor);
    painter.drawText(rect(), Qt::AlignCenter, QString(m_letter));
}

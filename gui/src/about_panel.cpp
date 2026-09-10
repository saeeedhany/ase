#include "about_panel.h"

#include "editor_viewport.h"
#include "letter_badge.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPalette>
#include <QPixmap>
#include <QVBoxLayout>

namespace {
/* No explicit colors in the markup (besides the links, which need
 * *some* color to read as clickable — reusing the app's single text
 * hue rather than inventing a "link blue") so the body text still
 * inherits the QLabel's own palette, same reasoning as HelpPanel. */
const char kAboutHtmlTemplate[] =
    "<p align=\"center\"><b>Absolute Simple Editor</b><br>"
    "Version %1 &mdash; in development, not yet publicly released</p>"
    "<p align=\"center\">A minimal, robust, blazingly fast, and "
    "aesthetically deliberate GUI text editor.</p>"
    "<p align=\"center\">Developed by <b>Saeed</b><br>"
    "<a href=\"%2\" style=\"color:%4;\">%2</a><br>"
    "<a href=\"%3\" style=\"color:%4;\">%3</a></p>"
    "<p align=\"center\">All phases of the \"complete normal editor\" "
    "pass are done — undo/redo, selection, clipboard, find/replace, "
    "editor chrome, and a command line with :compile. Full modal Vim "
    "emulation is planned next. The license is still an open decision, "
    "so this isn't published publicly yet.</p>";
}

AboutPanel::AboutPanel(EditorViewport *viewport) : FloatingPanel(viewport), m_viewport(viewport) {
    QWidget *content = contentWidget();
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(6);

    auto *headerRow = new QHBoxLayout();
    headerRow->setSpacing(8);
    m_badge = new LetterBadge(QLatin1Char('i'), content);
    headerRow->addWidget(m_badge);
    m_title = new QLabel(QStringLiteral("About"), content);
    QFont titleFont = m_title->font();
    titleFont.setBold(true);
    m_title->setFont(titleFont);
    headerRow->addWidget(m_title);
    headerRow->addStretch(1);
    layout->addLayout(headerRow);

    m_logo = new QLabel(content);
    QPixmap logo(QStringLiteral(":/ase.png"));
    if (!logo.isNull()) {
        m_logo->setPixmap(logo.scaledToWidth(180, Qt::SmoothTransformation));
    }
    m_logo->setAlignment(Qt::AlignHCenter);
    layout->addWidget(m_logo);

    m_body = new QLabel(content);
    m_body->setTextFormat(Qt::RichText);
    m_body->setWordWrap(true);
    m_body->setOpenExternalLinks(true);
    m_body->setMinimumWidth(440);
    m_body->setFocusPolicy(Qt::StrongFocus);
    /* Centered under the centered logo, not left-aligned — per direct
     * feedback that left-aligned text under a centered image looked
     * unaligned. See docs/adr/0028. */
    m_body->setAlignment(Qt::AlignHCenter);
    layout->addWidget(m_body);

    m_body->installEventFilter(this);
}

void AboutPanel::openAbout() {
    refreshTheme();
    setAnimated(m_viewport->animationsEnabled());
    openPanel();
    m_body->setFocus();
}

void AboutPanel::hideBar() {
    setAnimated(m_viewport->animationsEnabled());
    closePanel();
    m_viewport->setFocus();
}

void AboutPanel::refreshTheme() {
    setColors(m_viewport->panelBackgroundColor(), m_viewport->panelBorderColor());

    QColor badgeFill = m_viewport->textColor();
    badgeFill.setAlpha(220);
    m_badge->setColors(badgeFill, m_viewport->backgroundColor());

    /* QLabel's default palette text color doesn't follow this app's
     * custom dark theme at all — it rendered black regardless of
     * background, a real bug found by actually looking at a
     * screenshot. Every QLabel in this panel needs its palette set
     * explicitly; m_title was the one missed the first time around. */
    QPalette pal = m_title->palette();
    pal.setColor(QPalette::WindowText, m_viewport->textColor());
    m_title->setPalette(pal);
    m_body->setPalette(pal);

    m_body->setText(QString::fromUtf8(kAboutHtmlTemplate)
                         .arg(QStringLiteral("0.0.0"), QStringLiteral("https://github.com/saeeedhany"),
                              QStringLiteral("https://saeedz.vercel.app"), m_viewport->textColor().name()));
}

bool AboutPanel::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::KeyPress && watched == m_body) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            hideBar();
            return true;
        }
    }
    return FloatingPanel::eventFilter(watched, event);
}

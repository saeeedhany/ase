#include "about_panel.h"

#include "editor_viewport.h"
#include "letter_badge.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPalette>
#include <QPixmap>
#include <QSizePolicy>
#include <QVBoxLayout>

#ifndef ASE_VERSION_STRING
#define ASE_VERSION_STRING "0.0.0-dev" /* fallback if CMake didn't define it — see gui/CMakeLists.txt */
#endif

namespace {
/* No explicit colors in the markup (besides the links, which need
 * *some* color to read as clickable — reusing the app's single text
 * hue rather than inventing a "link blue") so the body text still
 * inherits the QLabel's own palette, same reasoning as HelpPanel. */
const char kAboutHtmlTemplate[] =
    "<p style=\"margin-top:0;\"><b>Absolute Simple Editor</b><br>"
    "<span style=\"color:%6;\">Version %1</span></p>"
    "<p>A minimal, robust, blazingly fast, and aesthetically deliberate "
    "GUI text editor.</p>"
    "<p><span style=\"color:%6;\">Built by</span> <b>Saeed</b><br>"
    "<a href=\"%2\" style=\"color:%5;\">%2</a><br>"
    "<a href=\"%3\" style=\"color:%5;\">%3</a><br>"
    "<a href=\"%4\" style=\"color:%5;\">Discord</a></p>"
    "<p style=\"margin-bottom:0;\"><span style=\"color:%6;\">"
    "Licensed under the Apache License 2.0.</span></p>";
}

AboutPanel::AboutPanel(EditorViewport *viewport) : FloatingPanel(viewport), m_viewport(viewport) {
    QWidget *content = contentWidget();
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(6);

    /* See docs/adr/0044 — a real QWidget so the whole bar (not just the
     * badge, docs/adr/0031's original handle) can be the drag handle;
     * badge/title marked transparent to mouse events so a press
     * anywhere across the bar still reaches it. */
    auto *headerBar = new QWidget(content);
    auto *headerRow = new QHBoxLayout(headerBar);
    headerRow->setContentsMargins(0, 0, 0, 0);
    headerRow->setSpacing(8);
    m_badge = new LetterBadge(QLatin1Char('i'), headerBar);
    m_badge->setAttribute(Qt::WA_TransparentForMouseEvents);
    headerRow->addWidget(m_badge);
    m_title = new QLabel(QStringLiteral("About"), headerBar);
    m_title->setAttribute(Qt::WA_TransparentForMouseEvents);
    QFont titleFont = m_title->font();
    titleFont.setBold(true);
    m_title->setFont(titleFont);
    headerRow->addWidget(m_title);
    headerRow->addStretch(1);
    layout->addWidget(headerBar);
    setDragHandle(headerBar);

    /* Two columns: the logo holds its own fixed column on the left, the
     * text runs left-aligned beside it. Previously both were stacked and
     * centered (docs/adr/0028) — centered prose is harder to read, since
     * every line starts at a different x and the eye has to re-find the
     * left edge on each one. Side-by-side gives the text a single hard
     * left edge to track while keeping the logo's presence. See
     * docs/adr/0055. */
    auto *bodyRow = new QWidget(content);
    auto *bodyLayout = new QHBoxLayout(bodyRow);
    bodyLayout->setContentsMargins(0, 4, 0, 0);
    bodyLayout->setSpacing(18);

    m_logo = new QLabel(bodyRow);
    QPixmap logo(QStringLiteral(":/ase.png"));
    if (!logo.isNull()) {
        m_logo->setPixmap(logo.scaledToWidth(132, Qt::SmoothTransformation));
    }
    /* Fixed, and pinned to the top of its column: the logo is a static
     * mark, so it must not stretch, re-center, or drift down as the text
     * beside it grows. */
    m_logo->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
    m_logo->setFixedWidth(132);
    m_logo->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    bodyLayout->addWidget(m_logo, 0, Qt::AlignTop);

    m_body = new QLabel(bodyRow);
    m_body->setTextFormat(Qt::RichText);
    m_body->setWordWrap(true);
    m_body->setOpenExternalLinks(true);
    m_body->setMinimumWidth(360);
    m_body->setFocusPolicy(Qt::StrongFocus);
    m_body->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    bodyLayout->addWidget(m_body, 1);

    layout->addWidget(bodyRow);

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

void AboutPanel::restoreFocusAfterDrag() {
    m_body->setFocus();
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

    /* Secondary lines (version, byline, licence) sit one opacity tier
     * down, the same "vary opacity, never hue" move the gutter and
     * comments already make (docs/adr/0007). Blended to a solid colour
     * rather than passed as #AARRGGBB: Qt's rich-text CSS does not
     * reliably parse an alpha channel in a hex colour, so the dimming
     * would silently come out fully opaque. */
    QColor dimText = m_viewport->textColor();
    QColor panelBg = m_viewport->panelBackgroundColor();
    const double dim = 150.0 / 255.0;
    dimText.setRgb(static_cast<int>(panelBg.red() + (dimText.red() - panelBg.red()) * dim),
                    static_cast<int>(panelBg.green() + (dimText.green() - panelBg.green()) * dim),
                    static_cast<int>(panelBg.blue() + (dimText.blue() - panelBg.blue()) * dim));

    m_body->setText(QString::fromUtf8(kAboutHtmlTemplate)
                         .arg(QStringLiteral(ASE_VERSION_STRING), QStringLiteral("https://github.com/saeeedhany"),
                              QStringLiteral("https://saeedz.vercel.app"),
                              QStringLiteral("https://discord.gg/sBkH45DzHc"), m_viewport->textColor().name(),
                              dimText.name()));
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

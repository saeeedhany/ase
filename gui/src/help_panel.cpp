#include "help_panel.h"

#include "editor_viewport.h"
#include "letter_badge.h"
#include "scrollbar_style.h"
#include "smooth_scroll.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPalette>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {
/* Maintained by hand, not generated from the keybinding dispatch code
 * — see the class doc comment. Two-column rows via a plain HTML table;
 * no explicit colors in the markup so it inherits the QLabel's own
 * palette (refreshTheme() sets that), keeping this consistent with the
 * "one font color" pillar the rest of the app already follows. */
/* Section -> rows of (keys, what it does). Rendered by buildHelpHtml()
 * below rather than written as one long HTML literal: the literal had
 * drifted badly out of date (no Vim mode, no buffers, no font zoom) —
 * partly because adding a row meant hand-writing four tags, which is
 * exactly the kind of friction that stops people updating docs. A table
 * of data is something you can actually keep honest.
 *
 * Still maintained by hand, not generated from the keybinding dispatch
 * — see the class doc comment for why. */
struct HelpRow {
    const char *keys;
    const char *description;
};
struct HelpSection {
    const char *title;
    /* Sections the user has to opt into are worth saying so, rather than
     * leaving someone hunting for a key that does nothing on their
     * setup. nullptr when it always applies. */
    const char *note;
    QVector<HelpRow> rows;
};

QVector<HelpSection> helpSections() {
    return {
        {"Navigation", nullptr,
         {{"&larr; &rarr; &uarr; &darr;", "Move cursor"},
          {"Shift + arrow", "Extend selection"},
          {"Home / End", "Line start / end"},
          {"Ctrl+D", "Select next occurrence"}}},

        {"Editing", nullptr,
         {{"Backspace / Delete", "Delete character or selection"},
          {"Ctrl+Z / Ctrl+Shift+Z", "Undo / redo"},
          {"Ctrl+C / Ctrl+X / Ctrl+V", "Copy / cut / paste"},
          {"Ctrl+A", "Select all"}}},

        {"Files &amp; buffers", nullptr,
         {{"Ctrl+N", "New file"},
          {"Ctrl+O", "Open"},
          {"Ctrl+S", "Save"},
          {"Ctrl+Shift+S", "Save as"},
          {"Ctrl+Tab / Ctrl+Shift+Tab", "Next / previous buffer"},
          {"Ctrl+W", "Close buffer"}}},

        {"Find &amp; replace", nullptr,
         {{"Ctrl+F", "Find"},
          {"Ctrl+H", "Find and replace"},
          {"Enter / Shift+Enter", "Next / previous match"},
          {"Ctrl+Enter", "Replace all (from the replace field)"}}},

        {"View", nullptr,
         {{"Ctrl+= / Ctrl+-", "Larger / smaller font"},
          {"Ctrl+0", "Reset font size"}}},

        {"Vim mode", "vim_mode = true",
         {{"Esc / i / a", "Normal mode / insert / append"},
          {"I / A / o / O", "Insert at line start / end, open line below / above"},
          {"h j k l / w b e", "Move by character / word"},
          {"0 / ^ / $", "Column 0 / first non-blank / line end"},
          {"gg / G / 3j", "First line / last line / with a count"},
          {"v", "Visual mode"},
          {"d y c + motion", "Delete / yank / change (dd, yy, cc for lines)"},
          {"x / p / P", "Delete character / paste after / before"},
          {"u / Ctrl+R", "Undo / redo"},
          {":", "Command line"}}},

        {"Command line", nullptr,
         {{"Ctrl+; &nbsp;(or : in Vim mode)", "Open command line"},
          {":w &nbsp; :q", "Save / quit"},
          {":compile &nbsp; :output", "Build / toggle the output panel"},
          {":42", "Jump to line 42"},
          {":&lt;name&gt;", "Run a plugin command"}}},

        {"Build", "build_command",
         {{"Ctrl+B", "Compile the current file"},
          {"Ctrl+Shift+O", "Toggle the output panel"}}},

        {"Language server", "lsp_command, .c/.h files",
         {{"(automatic)", "Diagnostics, and completion while typing"},
          {"&uarr; &darr;", "Move through completions"},
          {"Enter / Tab", "Accept completion"},
          {"Esc", "Dismiss completion"},
          {"(automatic)", "Hover info when the pointer rests on a symbol"}}},

        {"Panels", nullptr,
         {{"Ctrl+/", "This panel"},
          {"Ctrl+I", "About"},
          {"Esc", "Close the open panel, or collapse the selection"},
          {"Drag the header", "Move a panel"},
          {"Ctrl+Q", "Quit"}}},
    };
}

/* `dim` carries the section notes and the key column one opacity tier
 * down — same "vary opacity, never hue" move as everything else
 * (docs/adr/0007). Keys are given a fixed-width column and the
 * descriptions a common left edge, so the whole thing reads as two
 * aligned columns instead of ragged pairs. */
QString buildHelpHtml(const QString &textColor, const QString &dim) {
    QString html;
    const QVector<HelpSection> sections = helpSections();
    for (int i = 0; i < sections.size(); ++i) {
        const HelpSection &section = sections[i];
        html += QStringLiteral("<p style=\"margin-top:%1px; margin-bottom:4px;\">"
                                "<b style=\"color:%2;\">%3</b>")
                     .arg(i == 0 ? 0 : 14)
                     .arg(textColor, QString::fromUtf8(section.title));
        if (section.note != nullptr) {
            html += QStringLiteral("<span style=\"color:%1;\"> &nbsp;&mdash;&nbsp; %2</span>")
                         .arg(dim, QString::fromUtf8(section.note));
        }
        html += QStringLiteral("</p><table cellspacing=\"0\" cellpadding=\"3\" width=\"100%\">");
        for (const HelpRow &row : section.rows) {
            html += QStringLiteral("<tr>"
                                    "<td width=\"215\" style=\"color:%1;\">%2</td>"
                                    "<td style=\"color:%3;\">%4</td>"
                                    "</tr>")
                         .arg(dim, QString::fromUtf8(row.keys), textColor, QString::fromUtf8(row.description));
        }
        html += QStringLiteral("</table>");
    }
    return html;
}

} // namespace

HelpPanel::HelpPanel(EditorViewport *viewport) : FloatingPanel(viewport), m_viewport(viewport) {
    QWidget *content = contentWidget();
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(6);

    /* A real QWidget, not just a QHBoxLayout added directly to `layout`
     * — installEventFilter (setDragHandle's mechanism) needs an actual
     * widget to watch. The badge and title are marked transparent to
     * mouse events so a press anywhere across the bar reaches this
     * widget and starts a drag, not just a press on the badge itself
     * (docs/adr/0031's original, narrower handle) — see docs/adr/0044. */
    auto *headerBar = new QWidget(content);
    auto *headerRow = new QHBoxLayout(headerBar);
    headerRow->setContentsMargins(0, 0, 0, 0);
    headerRow->setSpacing(8);
    m_badge = new LetterBadge(QLatin1Char('?'), headerBar);
    m_badge->setAttribute(Qt::WA_TransparentForMouseEvents);
    headerRow->addWidget(m_badge);
    m_title = new QLabel(QStringLiteral("Keyboard shortcuts"), headerBar);
    m_title->setAttribute(Qt::WA_TransparentForMouseEvents);
    QFont titleFont = m_title->font();
    titleFont.setBold(true);
    m_title->setFont(titleFont);
    headerRow->addWidget(m_title);
    headerRow->addStretch(1);
    layout->addWidget(headerBar);
    setDragHandle(headerBar);

    m_body = new QLabel(content);
    m_body->setTextFormat(Qt::RichText);
    m_body->setWordWrap(true);
    /* Text is built in refreshTheme(), which is the only place that
     * knows the current theme colors — see buildHelpHtml(). */

    m_scrollArea = new QScrollArea(content);
    m_scrollArea->setWidget(m_body);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setMinimumSize(560, 460);
    m_scrollArea->setFocusPolicy(Qt::StrongFocus);
    layout->addWidget(m_scrollArea);

    m_scrollArea->installEventFilter(this);
    installSmoothScroll(m_scrollArea, m_viewport); /* see docs/adr/0031 */
}

void HelpPanel::openHelp() {
    refreshTheme();
    setAnimated(m_viewport->animationsEnabled());
    openPanel();
    m_scrollArea->setFocus();
}

void HelpPanel::hideBar() {
    setAnimated(m_viewport->animationsEnabled());
    closePanel();
    m_viewport->setFocus();
}

void HelpPanel::restoreFocusAfterDrag() {
    m_scrollArea->setFocus();
}

void HelpPanel::refreshTheme() {
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

    /* Blended to a solid colour rather than passed as #AARRGGBB: Qt's
     * rich-text CSS does not reliably parse an alpha channel in a hex
     * colour, so the dimming would silently render fully opaque. */
    QColor panelBg = m_viewport->panelFieldColor();
    QColor dim = m_viewport->textColor();
    const double dimFactor = 150.0 / 255.0;
    dim.setRgb(static_cast<int>(panelBg.red() + (dim.red() - panelBg.red()) * dimFactor),
                static_cast<int>(panelBg.green() + (dim.green() - panelBg.green()) * dimFactor),
                static_cast<int>(panelBg.blue() + (dim.blue() - panelBg.blue()) * dimFactor));
    m_body->setText(buildHelpHtml(m_viewport->textColor().name(), dim.name()));

    QPalette scrollPal = m_scrollArea->palette();
    scrollPal.setColor(QPalette::Base, m_viewport->panelFieldColor());
    scrollPal.setColor(QPalette::Window, m_viewport->panelFieldColor());
    m_scrollArea->setPalette(scrollPal);
    m_scrollArea->setAutoFillBackground(true);
    m_body->setAutoFillBackground(false);

    QColor handle = m_viewport->textColor();
    handle.setAlpha(90);
    QColor handleHover = m_viewport->textColor();
    handleHover.setAlpha(170);
    m_scrollArea->setStyleSheet(thinScrollBarStyleSheet(handle, handleHover));
}

bool HelpPanel::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::KeyPress && watched == m_scrollArea) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            hideBar();
            return true;
        }
    }
    return FloatingPanel::eventFilter(watched, event);
}

#include "help_panel.h"

#include "editor_viewport.h"
#include "letter_badge.h"
#include "scrollbar_style.h"
#include "smooth_scroll.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPalette>
#include <QLineEdit>
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
          {"(the + at the right of the tab strip)", "New file"},
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
          {"v / V", "Visual / visual line mode"},
          {"o", "Jump to the other end of the selection"},
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

    /* Shortcuts are how this editor is driven, so this panel is
     * something you come back to and scan — not read once. A search
     * field over every binding beats scrolling a wall of text. */
    m_search = new QLineEdit(content);
    m_search->setFrame(false);
    m_search->setPlaceholderText(QStringLiteral("Search shortcuts"));
    layout->addWidget(m_search);
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString &text) { applySearch(text); });

    m_sectionsHost = new QWidget(content);
    m_sectionsLayout = new QVBoxLayout(m_sectionsHost);
    m_sectionsLayout->setContentsMargins(0, 2, 0, 0);
    m_sectionsLayout->setSpacing(2);
    buildSections();
    m_sectionsLayout->addStretch(1);

    m_scrollArea = new QScrollArea(content);
    m_scrollArea->setWidget(m_sectionsHost);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setMinimumSize(580, 440);
    m_scrollArea->setFocusPolicy(Qt::StrongFocus);
    layout->addWidget(m_scrollArea);

    m_search->installEventFilter(this);
    m_scrollArea->installEventFilter(this);
    installSmoothScroll(m_scrollArea, m_viewport); /* see docs/adr/0031 */
}

void HelpPanel::openHelp() {
    m_search->clear(); /* every open starts from the full list */
    applySearch(QString());
    refreshTheme();
    setAnimated(m_viewport->animationsEnabled());
    openPanel();
    m_search->setFocus();
}

/* One header row plus one rows-label per section. The rows are a small
 * rich-text table rather than a widget per row: a section is shown or
 * hidden as a unit and its rows never need individual interaction, so
 * two widgets per section is the cheaper shape that still collapses. */
void HelpPanel::buildSections() {
    const QVector<HelpSection> data = helpSections();
    for (const HelpSection &source : data) {
        Section section;
        section.title = QString::fromUtf8(source.title);
        section.note = source.note != nullptr ? QString::fromUtf8(source.note) : QString();
        for (const HelpRow &row : source.rows) {
            section.rows.push_back({QString::fromUtf8(row.keys), QString::fromUtf8(row.description)});
        }

        section.header = new QWidget(m_sectionsLayout->parentWidget());
        section.header->setCursor(Qt::PointingHandCursor);
        auto *headerLayout = new QHBoxLayout(section.header);
        headerLayout->setContentsMargins(0, 6, 0, 2);
        headerLayout->setSpacing(0);
        section.titleLabel = new QLabel(section.header);
        section.titleLabel->setTextFormat(Qt::RichText);
        section.titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        headerLayout->addWidget(section.titleLabel);
        headerLayout->addStretch(1);
        section.header->installEventFilter(this);
        m_sectionsLayout->addWidget(section.header);

        section.rowsWidget = new QWidget(m_sectionsLayout->parentWidget());
        auto *rowsLayout = new QVBoxLayout(section.rowsWidget);
        rowsLayout->setContentsMargins(0, 0, 0, 0);
        rowsLayout->setSpacing(0);
        section.rowsLabel = new QLabel(section.rowsWidget);
        section.rowsLabel->setTextFormat(Qt::RichText);
        section.rowsLabel->setWordWrap(true);
        rowsLayout->addWidget(section.rowsLabel);
        m_sectionsLayout->addWidget(section.rowsWidget);

        m_sections.push_back(section);
    }
}

void HelpPanel::setSectionExpanded(int index, bool expanded) {
    if (index < 0 || index >= m_sections.size()) {
        return;
    }
    m_sections[index].expanded = expanded;
    m_sections[index].rowsWidget->setVisible(expanded && m_sections[index].visible);
    restyleSections();
}

/* Case-insensitive match against either column, plus the section title
 * so "vim" finds the whole Vim block. A section with no surviving rows
 * disappears entirely rather than sitting there as an empty heading, and
 * searching force-expands whatever still matches — collapsed state is a
 * browsing preference, not something that should hide results. */
void HelpPanel::applySearch(const QString &query) {
    QString needle = query.trimmed().toLower();
    for (Section &section : m_sections) {
        bool titleMatches = needle.isEmpty() || section.title.toLower().contains(needle);
        int matches = 0;
        for (const QPair<QString, QString> &row : section.rows) {
            if (needle.isEmpty() || titleMatches || row.first.toLower().contains(needle) ||
                row.second.toLower().contains(needle)) {
                ++matches;
            }
        }
        section.visible = matches > 0;
        section.header->setVisible(section.visible);
        section.rowsWidget->setVisible(section.visible && (section.expanded || !needle.isEmpty()));
    }
    restyleSections();
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

    QPalette searchPal = m_search->palette();
    searchPal.setColor(QPalette::Base, m_viewport->panelFieldColor());
    searchPal.setColor(QPalette::Text, m_viewport->textColor());
    QColor searchPlaceholder = m_viewport->textColor();
    searchPlaceholder.setAlpha(115);
    searchPal.setColor(QPalette::PlaceholderText, searchPlaceholder);
    m_search->setPalette(searchPal);

    restyleSections();

    QPalette scrollPal = m_scrollArea->palette();
    scrollPal.setColor(QPalette::Base, m_viewport->panelFieldColor());
    scrollPal.setColor(QPalette::Window, m_viewport->panelFieldColor());
    m_scrollArea->setPalette(scrollPal);
    m_scrollArea->setAutoFillBackground(true);
    /* The scroll area's *widget* needs the theme too. It used to be the
     * rich-text body, which inherited it; now it is a plain container,
     * and without this it painted Qt's default near-white behind
     * everything — which made the cream description column effectively
     * invisible while the colours themselves were perfectly correct. */
    m_sectionsHost->setPalette(scrollPal);
    m_sectionsHost->setAutoFillBackground(true);

    QColor handle = m_viewport->textColor();
    handle.setAlpha(90);
    QColor handleHover = m_viewport->textColor();
    handleHover.setAlpha(170);
    m_scrollArea->setStyleSheet(thinScrollBarStyleSheet(handle, handleHover));
}

bool HelpPanel::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::KeyPress && (watched == m_scrollArea || watched == m_search)) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            /* Escape clears an active search first — losing your filter
             * is a smaller surprise than losing the whole panel when you
             * only meant to start over. */
            if (watched == m_search && !m_search->text().isEmpty()) {
                m_search->clear();
                return true;
            }
            hideBar();
            return true;
        }
    }
    /* Click a section header to fold it away. */
    if (event->type() == QEvent::MouseButtonPress) {
        for (int i = 0; i < m_sections.size(); ++i) {
            if (watched == m_sections[i].header) {
                setSectionExpanded(i, !m_sections[i].expanded);
                return true;
            }
        }
    }
    return FloatingPanel::eventFilter(watched, event);
}

/* Renders every section's header and rows at the current theme. Colours
 * are blended to solid RGB rather than passed as #AARRGGBB: Qt's
 * rich-text CSS does not reliably parse an alpha channel in a hex
 * colour, so the dimming would silently render fully opaque. */
void HelpPanel::restyleSections() {
    if (m_sections.isEmpty()) {
        return;
    }
    QColor field = m_viewport->panelFieldColor();
    auto blend = [&field](QColor color, double factor) {
        color.setRgb(static_cast<int>(field.red() + (color.red() - field.red()) * factor),
                      static_cast<int>(field.green() + (color.green() - field.green()) * factor),
                      static_cast<int>(field.blue() + (color.blue() - field.blue()) * factor));
        return color;
    };
    QString strong = m_viewport->textColor().name();
    QString dim = blend(m_viewport->textColor(), 150.0 / 255.0).name();
    QString faint = blend(m_viewport->textColor(), 105.0 / 255.0).name();

    /* Copied from an already-themed widget, not default-constructed:
     * QPalette() picks up the *application* palette, whose other roles
     * then fight the theme — the rich text ended up rendering in a flat
     * light grey with the inline colours ignored. */
    QPalette pal = m_title->palette();
    pal.setColor(QPalette::WindowText, m_viewport->textColor());
    pal.setColor(QPalette::Text, m_viewport->textColor());

    for (Section &section : m_sections) {
        section.titleLabel->setPalette(pal);
        section.rowsLabel->setPalette(pal);

        /* A rotated caret rather than +/- or a chevron image: it reads as
         * "there is more under here" without adding an icon set to an app
         * that has none. */
        QString caret = section.expanded ? QStringLiteral("&#9662;") : QStringLiteral("&#9656;");
        QString header = QStringLiteral("<span style=\"color:%1;\">%2</span>&nbsp;&nbsp;"
                                         "<b style=\"color:%3;\">%4</b>")
                              .arg(faint, caret, strong, section.title);
        if (!section.note.isEmpty()) {
            header += QStringLiteral("<span style=\"color:%1;\"> &nbsp;&mdash;&nbsp; %2</span>")
                           .arg(faint, section.note);
        }
        section.titleLabel->setText(header);

        QString rows = QStringLiteral("<table cellspacing=\"0\" cellpadding=\"3\" width=\"100%\">");
        for (const QPair<QString, QString> &row : section.rows) {
            rows += QStringLiteral("<tr><td width=\"215\" style=\"color:%1;\">%2</td>"
                                    "<td style=\"color:%3;\">%4</td></tr>")
                         .arg(dim, row.first, strong, row.second);
        }
        rows += QStringLiteral("</table>");
        section.rowsLabel->setText(rows);
    }
}

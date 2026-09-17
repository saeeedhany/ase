#include "help_panel.h"

#include "keybindings.h"

#include "ase/theme.h"

#include "editor_viewport.h"

#include "ase/config.h"
#include "letter_badge.h"
#include "smooth_line_edit.h"
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
/* Section -> rows of (keys, what it does), rendered by buildHelpHtml().
 * Data rather than an HTML literal: the literal drifted badly out of
 * date, because adding a row meant hand-writing four tags.
 *
 * Maintained by hand, not generated from the keybinding dispatch — see
 * the class doc comment. */
struct HelpRow {
    QString keys; /* rich text: a literal row writes its own entities, a
                   * generated one is escaped at the point it is built */
    QString description;
};
struct HelpSection {
    const char *title;
    /* nullptr when the section always applies. */
    const char *note;
    QVector<HelpRow> rows;
};

/* Built from core's key table rather than written out here, so a key
 * cannot be added without this list showing it — see docs/adr/0088. */
HelpSection configSection() {
    size_t count = 0;
    const AseConfigKeyDoc *docs = ase_config_key_docs(&count);

    QVector<HelpRow> rows;
    rows.reserve(static_cast<int>(count) + 1);
    rows.push_back({QStringLiteral(":config"), QStringLiteral("Open your config file")});
    for (size_t i = 0; i < ase_theme_count(); i++) {
        const AseTheme *theme = ase_theme_at(i);
        rows.push_back({QStringLiteral("theme = %1").arg(QString::fromUtf8(theme->name)),
                        QString::fromUtf8(theme->summary).toHtmlEscaped()});
    }
    for (size_t i = 0; i < count; i++) {
        rows.push_back({QString::fromUtf8(docs[i].key).toHtmlEscaped(),
                        QString::fromUtf8(docs[i].summary).toHtmlEscaped()});
    }
    return {"Configuration", "~/.config/ase/config.ase &mdash; saved changes apply at once", rows};
}

/* The chord that actually runs a command, as the user's config has it.
 * The panel is the map of the keyboard; hardcoding the keys here meant
 * it went on describing the defaults after a rebinding. See
 * docs/adr/0113. */
static QString keysFor(const AseConfig *config, const char *command) {
    QStringList spelled;
    for (const QString &chord : keys::chordsFor(config, QString::fromLatin1(command))) {
        QStringList parts;
        for (const QString &part : chord.split(QLatin1Char('+'), Qt::SkipEmptyParts)) {
            static const QHash<QString, QString> kPretty = {
                {QStringLiteral("ctrl"), QStringLiteral("Ctrl")},
                {QStringLiteral("alt"), QStringLiteral("Alt")},
                {QStringLiteral("shift"), QStringLiteral("Shift")},
                {QStringLiteral("meta"), QStringLiteral("Meta")},
                {QStringLiteral("semicolon"), QStringLiteral(";")},
                {QStringLiteral("equal"), QStringLiteral("=")},
                {QStringLiteral("plus"), QStringLiteral("+")},
                {QStringLiteral("minus"), QStringLiteral("-")},
                {QStringLiteral("left"), QStringLiteral("Left")},
                {QStringLiteral("right"), QStringLiteral("Right")},
                {QStringLiteral("tab"), QStringLiteral("Tab")},
            };
            auto it = kPretty.constFind(part);
            parts << (it != kPretty.constEnd() ? it.value() : part.toUpper());
        }
        spelled << parts.join(QLatin1Char('+'));
    }
    if (spelled.isEmpty()) {
        return QStringLiteral("&mdash;"); /* switched off with `none` */
    }
    return spelled.join(QStringLiteral(" / ")).toHtmlEscaped();
}

QVector<HelpSection> helpSections(const AseConfig *config) {
    return {
        {"Navigation", nullptr,
         {{"&larr; &rarr; &uarr; &darr;", "Move cursor"},
          {"Shift + arrow", "Extend selection"},
          {"Home / End", "Line start / end"},
          {keysFor(config, "editor.cursor.add-next-occurrence"),
           "Select next occurrence (Insert mode / Vim off)"}}},

        {"Editing", nullptr,
         {{"Backspace / Delete", "Delete character or selection"},
          {keysFor(config, "editor.undo") + " / " + keysFor(config, "editor.redo"),
           "Undo / redo"},
          {keysFor(config, "editor.copy") + " / " + keysFor(config, "editor.cut") + " / " +
               keysFor(config, "editor.paste"),
           "Copy / cut / paste"},
          {keysFor(config, "editor.select-all"),
           "Select all (Vim Normal mode uses it to add one)"}}},

        {"Files &amp; buffers", nullptr,
         {{keysFor(config, "buffer.new"), "New file"},
          {"(the + at the right of the tab strip)", "New file"},
          {keysFor(config, "editor.open"), "Open"},
          {keysFor(config, "editor.open-in-project"), "Open any file in the project by name"},
          {keysFor(config, "editor.save"), "Save"},
          {keysFor(config, "editor.save-as"), "Save as"},
          {keysFor(config, "buffer.next") + " / " + keysFor(config, "buffer.previous"),
           "Next / previous buffer"},
          {keysFor(config, "buffer.close"), "Close buffer"}}},

        {"Find &amp; replace", nullptr,
         {{keysFor(config, "editor.find"), "Find"},
          {keysFor(config, "editor.find-in-project"), "Search every file in the project"},
          {keysFor(config, "editor.go-to-definition"),
           "Go to definition (needs a language server)"},
          {keysFor(config, "editor.find-references"),
           "Every use of the symbol under the cursor"},
          {keysFor(config, "editor.document-symbols"), "Outline of this file"},
          {keysFor(config, "editor.jump-back") + " / " + keysFor(config, "editor.jump-forward"),
           "Jump back / forward"},
          {keysFor(config, "editor.replace"), "Find and replace"},
          {"Enter / Shift+Enter", "Next / previous match"},
          {"Ctrl+Enter", "Replace all (from the replace field)"},
          {"n / N", "Repeat the last search, forwards / backwards"},
          {"", "&mdash; works with the bar closed, however you searched"}}},

        {"View", nullptr,
         {{keysFor(config, "editor.font.larger") + " / " + keysFor(config, "editor.font.smaller"),
           "Larger / smaller font"},
          {keysFor(config, "editor.font.reset"), "Reset font size"}}},

        {"Vim mode", "vim_mode = true",
         {{"Esc / i / a", "Normal mode / insert / append"},
          {"I / A / o / O", "Insert at line start / end, open line below / above"},
          {"h j k l / w b e", "Move by character / word"},
          {"W B E", "Move by WORD, split on blanks only"},
          {"0 / ^ / $", "Column 0 / first non-blank / line end"},
          {"{ / }", "Previous / next blank line"},
          {"%", "Jump to the matching bracket"},
          {"f F t T / ; ,", "Find a character in this line / repeat, reverse"},
          {"/ &nbsp; ?", "Search forwards / backwards"},
          {"n / N", "Next / previous match, in the search's own direction"},
          {"* / #", "Search for the word under the cursor, forwards / back"},
          {"Esc", "Clear the search highlight (the search itself is kept)"},
          {"Ctrl+U / Ctrl+D", "Half a screen up / down"},
          {"gg / G / 3j", "First line / last line / with a count"},
          {"gd", "Go to definition"},
          {"m &lt;letter&gt;", "Set a mark here"},
          {"` &lt;letter&gt;", "Jump to a mark, exact line and column"},
          {"' &lt;letter&gt;", "Jump to a mark's line, first non-blank"},
          {"d' &nbsp; d` &nbsp; y' &nbsp; c'", "Operate from the cursor to a mark"},
          {"m &lt;A-Z&gt;", "Set a mark that remembers its file too"},
          {"q &lt;letter&gt; ... q", "Record a macro into a register"},
          {"@ &lt;letter&gt; &nbsp; @@", "Play a macro / replay the last one"},
          {"v / V", "Visual / visual line mode"},
          {"o", "Jump to the other end of the selection"},
          {"r &nbsp;(in Visual)", "Replace every character in the selection"},
          {"d y c + motion", "Delete / yank / change (dd, yy, cc for lines)"},
          {"J &nbsp; 3J &nbsp; gJ", "Join lines with a space / three of them / verbatim"},
          {"\" &lt;letter&gt;", "Use a named register: \"ayy then \"ap"},
          {"iw aw &nbsp; iW aW", "Text object: word / word with its spaces"},
          {"i( i{ i[ i&lt; &nbsp; a(", "Text object: inside a bracket pair / around it"},
          {"i\" i' i` &nbsp; a\"", "Text object: inside quotes / around them"},
          {"", "&mdash; all four fill the unnamed register that p pastes"},
          {"~ &nbsp; 5~", "Toggle the case of a character / of five"},
          {"&gt;&gt; &nbsp; &lt;&lt; &nbsp; &gt;j", "Indent / unindent lines by four spaces"},
          {"Ctrl+A / Ctrl+X", "Add one to the next number on the line / subtract"},
          {"", "&mdash; these two shadow select-all and cut, in Normal mode only"},
          {"x / X", "Delete the character under / before the cursor"},
          {"s / S", "Change the character / the whole line"},
          {"C / D", "Change / delete to the end of the line"},
          {"p / P", "Paste after / before"},
          {"r / R", "Replace one character / keep replacing until Esc"},
          {".", "Repeat the last change (a count replaces the original)"},
          {"Ctrl+V", "Paste the system clipboard (y copies there too)"},
          {"u / Ctrl+R", "Undo / redo"},
          {":", "Command line"}}},

        {"Command line", nullptr,
         {{"Ctrl+; &nbsp;(or : in Vim mode)", "Open command line"},
          {":w", "Save"},
          {":q &nbsp; :q!", "Close this buffer / discard changes and close"},
          {":compile &nbsp; :output", "Build / toggle the output panel"},
          {":config", "Open your config file"},
          {":theme", "List the built-in palettes"},
          {":theme &lt;name&gt;", "Switch palette for this session"},
          {":theme save", "Keep the current palette in your config"},
          {":42", "Jump to line 42"},
          {":s/old/new/g", "Substitute on this line (g = every match)"},
          {":%s/old/new/g", "Substitute in the whole file (or :1,10s/…)"},
          {":&lt;name&gt;", "Run a plugin command"}}},

        {"Build", "build_command",
         {{"Ctrl+B", "Compile the current file"},
          {"Ctrl+Shift+O", "Toggle the output panel"}}},

        {"Language server", "lang.&lt;id&gt;.lsp, C and C++ files",
         {{"(automatic)", "Diagnostics, and completion while typing"},
          {"&uarr; &darr;", "Move through completions"},
          {"Enter / Tab", "Accept completion"},
          {"Ctrl+J / Ctrl+K", "Move down / up any list (completion, panels)"},
          {"Esc", "Dismiss completion"},
          {"(automatic)", "Hover info when the pointer rests on a symbol"}}},

        configSection(),

        {"Panels", nullptr,
         {{"F1", "This panel"},
          {"Alt+I", "About"},
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

    /* A real widget, since installEventFilter needs one to watch. The
     * badge and title are transparent to mouse events, so a press
     * anywhere on the bar starts a drag. */
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

    /* This panel is scanned repeatedly, not read once. */
    m_search = new SmoothLineEdit(content);
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

/* Two widgets per section: a section collapses as a unit and its rows
 * never need individual interaction. */
void HelpPanel::buildSections() {
    const QVector<HelpSection> data = helpSections(m_viewport != nullptr ? m_viewport->config() : nullptr);
    for (const HelpSection &source : data) {
        Section section;
        section.title = QString::fromUtf8(source.title);
        section.note = source.note != nullptr ? QString::fromUtf8(source.note) : QString();
        for (const HelpRow &row : source.rows) {
            section.rows.push_back({row.keys, row.description});
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

/* Matches either column plus the section title, so "vim" finds the
 * whole block. A section whose title does not match is narrowed to the
 * rows that do, which is what makes a long list searchable rather than
 * merely locatable. Searching force-expands matches: collapsed state is
 * a browsing preference, not a filter. */
void HelpPanel::applySearch(const QString &query) {
    QString needle = query.trimmed().toLower();
    m_searchNeedle = needle;
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

    /* QLabel's default palette renders black whatever the background,
     * so every label here needs its palette set explicitly. */
    QPalette pal = m_title->palette();
    pal.setColor(QPalette::WindowText, m_viewport->textColor());
    m_title->setPalette(pal);

    m_search->applyPanelTheme(m_viewport);

    restyleSections();

    QPalette scrollPal = m_scrollArea->palette();
    scrollPal.setColor(QPalette::Base, m_viewport->panelFieldColor());
    scrollPal.setColor(QPalette::Window, m_viewport->panelFieldColor());
    m_scrollArea->setPalette(scrollPal);
    m_scrollArea->setAutoFillBackground(true);
    /* The scroll area's widget needs the theme too, or it paints Qt's
     * near-white behind everything. */
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
            /* Escape clears an active search before closing. */
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

/* Colours are blended to solid RGB: Qt's rich-text CSS does not
 * reliably parse an alpha channel, so dimming rendered opaque. */
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

    /* Copied from an already-themed widget: a default QPalette picks
     * up the application palette, whose roles fight the theme. */
    QPalette pal = m_title->palette();
    pal.setColor(QPalette::WindowText, m_viewport->textColor());
    pal.setColor(QPalette::Text, m_viewport->textColor());

    for (Section &section : m_sections) {
        section.titleLabel->setPalette(pal);
        section.rowsLabel->setPalette(pal);

        /* A rotated caret, so the app needs no icon set. */
        QString caret = section.expanded ? QStringLiteral("&#9662;") : QStringLiteral("&#9656;");
        QString header = QStringLiteral("<span style=\"color:%1;\">%2</span>&nbsp;&nbsp;"
                                         "<b style=\"color:%3;\">%4</b>")
                              .arg(faint, caret, strong, section.title);
        if (!section.note.isEmpty()) {
            header += QStringLiteral("<span style=\"color:%1;\"> &nbsp;&mdash;&nbsp; %2</span>")
                           .arg(faint, section.note);
        }
        section.titleLabel->setText(header);

        bool titleMatches =
            m_searchNeedle.isEmpty() || section.title.toLower().contains(m_searchNeedle);

        QString rows = QStringLiteral("<table cellspacing=\"0\" cellpadding=\"3\" width=\"100%\">");
        for (const QPair<QString, QString> &row : section.rows) {
            if (!titleMatches && !row.first.toLower().contains(m_searchNeedle) &&
                !row.second.toLower().contains(m_searchNeedle)) {
                continue;
            }
            rows += QStringLiteral("<tr><td width=\"215\" style=\"color:%1;\">%2</td>"
                                    "<td style=\"color:%3;\">%4</td></tr>")
                         .arg(dim, row.first, strong, row.second);
        }
        rows += QStringLiteral("</table>");
        section.rowsLabel->setText(rows);
    }
}

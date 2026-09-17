#include "editor_viewport.h"

#include "keybindings.h"

#include "ase/theme.h"

#include "about_panel.h"
#include "command_line.h"
#include "completion_popup.h"
#include "file_browser_panel.h"
#include "find_bar.h"
#include "help_panel.h"
#include "hover_panel.h"
#include "motion.h"
#include "output_panel.h"

#include <algorithm>
#include <cstdlib>

#include <QFileInfo>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QTimer>

namespace {
/* Ctrl+=/Ctrl+- clamp range — see docs/adr/0050. Generous enough to be
 * genuinely useful (a real "I can't read this" zoom, or a presentation/
 * screen-share bump) without allowing a value so small/large it breaks
 * layout math elsewhere (gutter width, scroll margins, ...). */
constexpr int kMinFontSize = 6;
constexpr int kMaxFontSize = 72;
} // namespace

void EditorViewport::loadConfig() {
    char *path = ase_config_default_path();
    if (path != nullptr) {
        m_configPath = QString::fromLocal8Bit(path);
        free(path);
        ase_config_write_default_if_missing(m_configPath.toUtf8().constData());
    }

    rebuildConfig();
    applyConfig();

    if (!m_configPath.isEmpty()) {
        m_configModified = QFileInfo(m_configPath).lastModified();
    }
}

void EditorViewport::rebuildConfig() {
    m_config = ase_config_load(m_configPath.isEmpty() ? nullptr : m_configPath.toUtf8().constData());

    /* Under the file's own colours, never over them. A session theme
     * from `:theme` layers in exactly the same place as `theme =` in the
     * config, which is what makes previewing one and saving it produce
     * the same screen. See docs/adr/0114. */
    QString theme = m_sessionTheme;
    if (theme.isEmpty()) {
        const char *configured = ase_config_get_string(m_config, "theme");
        theme = configured != nullptr ? QString::fromUtf8(configured) : QString();
    }
    if (!theme.isEmpty() && !ase_config_apply_theme(m_config, theme.toUtf8().constData())) {
        QString name = theme;
        QTimer::singleShot(0, this, [this, name]() {
            notify(NotifyLevel::Warning, QStringLiteral("no theme called '%1'").arg(name));
        });
    }

    m_projectConfigPath.clear();
    m_projectConfigModified = QDateTime();
    if (m_filePath.isEmpty()) {
        return;
    }

    QByteArray absolute = QFileInfo(m_filePath).absoluteFilePath().toUtf8();
    char *found = ase_config_find_project_file(absolute.constData());
    if (found == nullptr) {
        return;
    }
    m_projectConfigPath = QString::fromLocal8Bit(found);
    free(found);

    size_t refused = 0;
    ase_config_overlay_project(m_config, m_projectConfigPath.toUtf8().constData(), &refused);
    m_projectConfigModified = QFileInfo(m_projectConfigPath).lastModified();

    if (refused > 0) {
        /* Queued: the first call runs from the constructor, before
         * anything is connected to messagePosted. See docs/adr/0087. */
        QString message = QStringLiteral("%1: ignored %2 key%3 a project file may not set")
                              .arg(QLatin1String(ASE_PROJECT_CONFIG_NAME))
                              .arg(refused)
                              .arg(refused == 1 ? QString() : QStringLiteral("s"));
        QTimer::singleShot(0, this, [this, message]() { notify(NotifyLevel::Warning, message); });
    }
}

/* Said once, when the config is read, rather than when a key is pressed:
 * a binding that names nothing is a mistake in the file, and the file is
 * what the user is looking at. Silence would leave a dead key looking
 * like a bug in the editor. */
void EditorViewport::reportKeybindingProblems() {
    if (m_commands == nullptr) {
        return;
    }
    /* Plugin commands are registered with the host, not the registry, so
     * they have to be named here or every binding to one reads as a
     * mistake. */
    QStringList fromPlugins;
    for (size_t i = 0; i < ase_plugin_host_command_count(m_pluginHost); i++) {
        const char *name = ase_plugin_host_command_name(m_pluginHost, i);
        if (name != nullptr) {
            fromPlugins << QString::fromUtf8(name);
        }
    }
    /* The window's table is the one passed in; this viewport's own
     * names and the plugin host's are the "also known" list. */
    fromPlugins += m_ownCommands.names();
    const QStringList problems = keys::problems(m_config, *m_commands, fromPlugins);
    if (problems.isEmpty()) {
        return;
    }
    notify(NotifyLevel::Warning,
           problems.size() == 1
               ? problems.first()
               : QStringLiteral("%1 (and %2 more)").arg(problems.first()).arg(problems.size() - 1));
}

void EditorViewport::applyConfig() {
    uint8_t r, g, b, a;
    if (ase_config_get_color(m_config, "background", &r, &g, &b, &a)) {
        m_backgroundColor = QColor(r, g, b, a);
    }
    if (ase_config_get_color(m_config, "text", &r, &g, &b, &a)) {
        m_textColor = QColor(r, g, b, a);
    }
    if (ase_config_get_color(m_config, "selection", &r, &g, &b, &a)) {
        m_selectionColor = QColor(r, g, b, a);
    }
    if (ase_config_get_color(m_config, "find_match", &r, &g, &b, &a)) {
        m_findMatchColor = QColor(r, g, b, a);
    }
    if (ase_config_get_color(m_config, "panel_background", &r, &g, &b, &a)) {
        m_panelBackgroundColor = QColor(r, g, b, a);
    }
    if (ase_config_get_color(m_config, "diagnostic_error", &r, &g, &b, &a)) {
        m_diagnosticErrorColor = QColor(r, g, b, a);
    }
    if (ase_config_get_color(m_config, "diagnostic_warning", &r, &g, &b, &a)) {
        m_diagnosticWarningColor = QColor(r, g, b, a);
    }
    /* The two deliberate departures from the "one font color" pillar —
     * see docs/adr/0048. */
    if (ase_config_get_color(m_config, "syntax_type", &r, &g, &b, &a)) {
        m_syntaxTypeColor = QColor(r, g, b, a);
    }
    if (ase_config_get_color(m_config, "syntax_string", &r, &g, &b, &a)) {
        m_syntaxStringColor = QColor(r, g, b, a);
    }

    const char *familyStr = ase_config_get_string(m_config, "font_family");
    m_fontFamily = familyStr != nullptr ? QString::fromUtf8(familyStr) : QStringLiteral("monospace");
    long configuredSize = ase_config_get_int(m_config, "font_size", 11);
    /* An active runtime zoom (Ctrl+=/Ctrl+-, docs/adr/0050) survives a
     * config hot-reload of some *unrelated* setting — only Ctrl+0 (or
     * restarting) goes back to whatever font_size the file says. */
    rebuildFont(m_fontSizeOverride > 0 ? m_fontSizeOverride : static_cast<int>(configuredSize));

    /* Opt-in, off by default — see docs/adr/0012, decision 2. */
    const char *animationsStr = ase_config_get_string(m_config, "animations");
    m_animationsEnabled = animationsStr != nullptr &&
                          QString::fromUtf8(animationsStr).compare(QLatin1String("true"), Qt::CaseInsensitive) == 0;

    /* Opt-in, off by default, same reasoning and same pattern as
     * animations above — this changes what every keystroke does, so it
     * has to be asked for. See docs/adr/0046. */
    const char *vimModeStr = ase_config_get_string(m_config, "vim_mode");
    m_vimModeEnabled = vimModeStr != nullptr &&
                       QString::fromUtf8(vimModeStr).compare(QLatin1String("true"), Qt::CaseInsensitive) == 0;

    /* On by default, unlike animations — see docs/adr/0014, decision 4.
     * An unrecognized value falls back to "absolute" rather than
     * treating it as an error — a bad config value should never break
     * the editor. */
    /* The timer itself is re-intervalled by refreshAnimationClock(), which
     * also runs on the config poll — applyConfig() is called from the
     * constructor, before the timer exists. */
    motion::setMaxFps(static_cast<int>(ase_config_get_int(m_config, "max_fps", 0)));

    const char *lineNumbersStr = ase_config_get_string(m_config, "line_numbers");
    m_lineNumberMode = lineNumbersStr != nullptr ? QString::fromUtf8(lineNumbersStr).toLower()
                                                  : QStringLiteral("absolute");
    if (m_lineNumberMode != QLatin1String("off") && m_lineNumberMode != QLatin1String("relative")) {
        m_lineNumberMode = QStringLiteral("absolute");
    }
}

void EditorViewport::rebuildFont(int pointSize) {
    m_font = m_fontFamily.compare(QLatin1String("monospace"), Qt::CaseInsensitive) == 0
                 ? QFontDatabase::systemFont(QFontDatabase::FixedFont)
                 : QFont(m_fontFamily);
    m_font.setPointSize(pointSize);

    /* Cached once here rather than reconstructed per run per paint — see
     * docs/adr/0017. */
    m_metrics = QFontMetrics(m_font);
    QFont boldFont = m_font;
    boldFont.setBold(true);
    m_boldMetrics = QFontMetrics(boldFont);

    m_lineHeight = m_metrics.height();
    m_charWidth = m_metrics.horizontalAdvance(QLatin1Char('M'));
}

void EditorViewport::adjustFontSize(int delta) {
    int next = std::clamp(m_font.pointSize() + delta, kMinFontSize, kMaxFontSize);
    if (next == m_font.pointSize()) {
        return;
    }
    m_fontSizeOverride = next;
    rebuildFont(next);
    ensureCursorVisible();
    snapAnimationToTarget();
    update();
}

void EditorViewport::resetFontSize() {
    if (m_fontSizeOverride == 0) {
        return;
    }
    m_fontSizeOverride = 0;
    rebuildFont(static_cast<int>(ase_config_get_int(m_config, "font_size", 11)));
    ensureCursorVisible();
    snapAnimationToTarget();
    update();
}

/* Polled rather than wired to QWindow::screenChanged: the window has no
 * native handle yet when the viewport is built, so connecting there
 * silently never fires. Re-reading a cached rate every 750ms costs
 * nothing and is right after a drag to another display. */
void EditorViewport::refreshAnimationClock() {
    motion::refreshTickFromScreen(this);
    int interval = motion::tickMs();
    if (m_blinkTimer != nullptr && m_blinkTimer->interval() != interval) {
        m_blinkTimer->setInterval(interval);
    }
}

void EditorViewport::checkConfigReload() {
    refreshAnimationClock();

    QDateTime modified = m_configPath.isEmpty() ? QDateTime() : QFileInfo(m_configPath).lastModified();
    bool userChanged = !m_configPath.isEmpty() && modified.isValid() && modified != m_configModified;

    /* Re-resolved rather than remembered, so a .ase.conf that appears
     * after the file was opened — or a nearer one — is picked up. It is
     * a handful of stat()s on the same timer that already stats one. */
    QString projectPath;
    if (!m_filePath.isEmpty()) {
        QByteArray absolute = QFileInfo(m_filePath).absoluteFilePath().toUtf8();
        char *found = ase_config_find_project_file(absolute.constData());
        if (found != nullptr) {
            projectPath = QString::fromLocal8Bit(found);
            free(found);
        }
    }
    QDateTime projectModified =
        projectPath.isEmpty() ? QDateTime() : QFileInfo(projectPath).lastModified();
    bool projectChanged =
        projectPath != m_projectConfigPath || projectModified != m_projectConfigModified;

    if (!userChanged && !projectChanged) {
        return;
    }
    if (userChanged) {
        m_configModified = modified;
    }

    const char *before = ase_config_language_for_path(m_config, m_filePath.toUtf8().constData());
    QString languageBefore = before != nullptr ? QString::fromUtf8(before) : QString();

    ase_config_destroy(m_config);
    rebuildConfig();
    applyConfig();

    const char *after = ase_config_language_for_path(m_config, m_filePath.toUtf8().constData());
    QString languageAfter = after != nullptr ? QString::fromUtf8(after) : QString();
    if (languageAfter != languageBefore) {
        rebuildSyntax();
        refreshCache();
    }
    /* The hot-reload has always been silent, which is fine when the
     * change is visible (a colour) and confusing when it isn't (a
     * keybinding-adjacent setting, or a typo that made the file parse to
     * defaults). */
    notify(NotifyLevel::Info, QStringLiteral("config reloaded"));
    repaintForNewTheme();
}

/*
 * Everything that has to be told the colours changed.
 *
 * The panels each hold their own palette, and the status bar and buffer
 * bar are repainted by the window's statusChanged handler — which is why
 * ensureCursorVisible() is in here rather than looking like a stray
 * scroll: it is what emits that signal. Without it the editor recolours
 * and its chrome does not, until the caret happens to move.
 *
 * One function because there are two ways the colours change — the
 * config file being edited, and `:theme` — and the second one was
 * written without half of this. See docs/adr/0115.
 */
void EditorViewport::repaintForNewTheme() {
    if (m_findBar != nullptr) {
        m_findBar->refreshTheme();
    }
    if (m_fileBrowser != nullptr) {
        m_fileBrowser->refreshTheme();
    }
    if (m_commandLine != nullptr) {
        m_commandLine->refreshTheme();
    }
    if (m_outputPanel != nullptr) {
        m_outputPanel->refreshTheme();
    }
    if (m_helpPanel != nullptr) {
        m_helpPanel->refreshTheme();
    }
    if (m_aboutPanel != nullptr) {
        m_aboutPanel->refreshTheme();
    }
    if (m_completionPopup != nullptr) {
        m_completionPopup->refreshTheme();
    }
    if (m_hoverPanel != nullptr) {
        m_hoverPanel->refreshTheme();
    }
    ensureCursorVisible(); /* emits statusChanged — see above */
    update();
}

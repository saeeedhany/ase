#include "editor_viewport.h"

#include "about_panel.h"
#include "command_line.h"
#include "completion_popup.h"
#include "file_browser_panel.h"
#include "find_bar.h"
#include "help_panel.h"
#include "hover_panel.h"
#include "output_panel.h"

#include <algorithm>
#include <cstdlib>

#include <QFileInfo>
#include <QFontDatabase>
#include <QFontMetrics>

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

    m_config = ase_config_load(m_configPath.isEmpty() ? nullptr : m_configPath.toUtf8().constData());
    applyConfig();

    if (!m_configPath.isEmpty()) {
        m_configModified = QFileInfo(m_configPath).lastModified();
    }
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

void EditorViewport::checkConfigReload() {
    if (m_configPath.isEmpty()) {
        return;
    }

    QDateTime modified = QFileInfo(m_configPath).lastModified();
    if (!modified.isValid() || modified == m_configModified) {
        return;
    }
    m_configModified = modified;

    ase_config_destroy(m_config);
    m_config = ase_config_load(m_configPath.toUtf8().constData());
    applyConfig();
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
    ensureCursorVisible();
    update();
}

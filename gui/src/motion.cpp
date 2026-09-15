#include "motion.h"

#include <algorithm>
#include <cmath>

#include <QGuiApplication>
#include <QScreen>
#include <QWidget>

namespace {
/* 60Hz until a screen says otherwise, which is the safe assumption when
 * a platform plugin reports nothing. */
int g_displayTickMs = 16;
/* 0 = follow the display. */
int g_capTickMs = 0;

/* Below 24Hz is not a display; above 480 is a misreport. Outside that,
 * keep what we have rather than trusting it. */
constexpr double kMinRefreshHz = 24.0;
constexpr double kMaxRefreshHz = 480.0;
/* 4ms floor: past ~240Hz the timer's own resolution dominates, and the
 * repaint cost stops buying smoothness. */
constexpr int kMinTickMs = 4;
constexpr int kMaxTickMs = 33;
} // namespace

namespace motion {

int tickMs() { return g_capTickMs > g_displayTickMs ? g_capTickMs : g_displayTickMs; }

int ticksFor(int ms) {
    int ticks = ms / tickMs();
    return ticks > 0 ? ticks : 1;
}

void setMaxFps(int fps) {
    g_capTickMs = fps > 0 ? std::clamp(1000 / fps, kMinTickMs, 1000) : 0;
}

void refreshTickFromScreen(const QWidget *widget) {
    const QScreen *screen = widget != nullptr ? widget->screen() : QGuiApplication::primaryScreen();
    if (screen == nullptr) {
        return;
    }
    double hz = screen->refreshRate();
    if (hz < kMinRefreshHz || hz > kMaxRefreshHz) {
        return;
    }
    g_displayTickMs = std::clamp(static_cast<int>(std::lround(1000.0 / hz)), kMinTickMs, kMaxTickMs);
}

} // namespace motion

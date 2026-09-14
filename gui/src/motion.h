#ifndef ASE_MOTION_H
#define ASE_MOTION_H

#include <QEasingCurve>
#include <QPropertyAnimation>

/*
 * The app's motion language, in one place. See docs/adr/0053.
 *
 * The rule: new animated chrome picks an existing tier below, it does
 * not invent a duration. Adding a tier is a deliberate change to the
 * motion language, not something to slip in.
 */
namespace motion {

/* One curve for everything; this must never vary per widget. */
constexpr QEasingCurve::Type kCurve = QEasingCurve::OutCubic;

/* ---- Duration tiers, named by role so new code can pick by intent ---- */

/* A local change inside a visible surface: a row highlight moving. */
constexpr int kQuick = 85;

/* Appearing or disappearing in place, with no travel. */
constexpr int kFade = 90;

/* Chrome that moves or opens. */
constexpr int kChrome = 110;

/* Viewport-scale travel: the longest distance, the longest tier. */
constexpr int kScroll = 140;

/* Scale + fade, never a bare fade — see docs/adr/0022. */
constexpr double kPopScale = 0.96;

/* ---- Frame-driven motion (EditorViewport only) ---- */

/*
 * The viewport eases per paint, multiplying each value's remaining
 * distance by kEaseFactor — exponential decay with no fixed end, so
 * input arriving mid-flight needs no cancel-and-restart.
 *
 * Tuned to match the tiers above perceptually: ~95% of the distance in
 * about 4 frames (~120ms). Raising it speeds up every eased value in
 * the viewport at once.
 */
constexpr double kEaseFactor = 0.68;

/* The viewport's animation heartbeat. Every frame-driven value — the
 * caret breathe cycle, the typing pop-in, all eased glide — advances one
 * step per tick of this, so it is the unit those tick counts are
 * expressed in. */
constexpr int kTickMs = 30;

/* Below this remaining fraction, an eased 0..1 opacity/reveal snaps to
 * its target instead of asymptotically crawling forever. */
constexpr double kOpacitySnapThreshold = 0.01;

/* Configures a Qt animation with the app's curve and one of the tiers
 * above. Prefer this over setting duration/curve by hand — it is what
 * makes "inherits the existing properties" the path of least resistance
 * rather than something each author has to remember. */
inline void apply(QPropertyAnimation *animation, int durationMs) {
    animation->setDuration(durationMs);
    animation->setEasingCurve(kCurve);
}

} // namespace motion

#endif /* ASE_MOTION_H */

#ifndef ASE_MOTION_H
#define ASE_MOTION_H

#include <QEasingCurve>
#include <QPropertyAnimation>

/*
 * The app's motion language, in one place — see docs/adr/0053.
 *
 * Before this header the same decisions were re-made independently in
 * six files: floating_panel.cpp (110ms), tracking_popup.cpp (90ms fade,
 * 110ms move), smooth_scroll.cpp (140ms), file_browser_panel.cpp (85ms),
 * and editor_viewport's own frame-driven easing. They happened to agree
 * on the curve, which is the part that carries most of the "feel" — but
 * only by each author looking at what the last one did. Nothing made
 * that inheritance automatic, and nothing stopped the next widget from
 * quietly picking a seventh number.
 *
 * The rule this header exists to enforce: **new animated chrome picks an
 * existing tier below. It does not invent a duration.** Adding a tier is
 * allowed, but it is a deliberate change to the app's motion language and
 * should be argued for, not slipped in.
 *
 * Values here are exactly the ones already tuned and accepted across
 * docs/adr/0022, 0024, 0027 and 0043 — this header centralises them, it
 * does not retune them.
 */
namespace motion {

/* Everything decelerates on the same curve. This is the single biggest
 * contributor to motion feeling like one app rather than several, and
 * it is the one thing that must never vary per widget. */
constexpr QEasingCurve::Type kCurve = QEasingCurve::OutCubic;

/* ---- Duration tiers, named by role so new code can pick by intent ---- */

/* A small, local state change inside an already-visible surface —
 * a highlighted row moving between entries. */
constexpr int kQuick = 85;

/* Something appearing or disappearing in place (no travel): a tracking
 * popup fading in/out. */
constexpr int kFade = 90;

/* Chrome that moves or opens: floating panels' scale+fade, a tracking
 * popup repositioning itself to follow the caret. */
constexpr int kChrome = 110;

/* Viewport-scale travel — the longest distance anything covers, so it
 * gets the longest tier. */
constexpr int kScroll = 140;

/* Panels open/close scaling from this fraction of full size, paired with
 * a fade. The "scale + fade, not a bare fade" rule from docs/adr/0022 —
 * a fade alone reads as flat next to everything else here. */
constexpr double kPopScale = 0.96;

/* ---- Frame-driven motion (EditorViewport only) ---- */

/*
 * The viewport animates per paint rather than per QPropertyAnimation: it
 * multiplies each value's *remaining distance* by kEaseFactor every
 * frame. That is a different model from the tiers above (exponential
 * decay with no fixed end time, vs. a fixed-duration curve), chosen
 * because the caret and scroll have to stay responsive to input that
 * arrives mid-flight — a fixed-duration animation would have to be
 * cancelled and restarted on every keystroke.
 *
 * Both models are deliberately tuned to land in the same perceptual
 * ballpark: at kTickMs, kEaseFactor covers ~95% of the distance in about
 * 4 frames (~120ms), which is why viewport glide and kChrome chrome read
 * as the same speed despite the different maths.
 *
 * Raising kEaseFactor speeds up every eased value in the viewport at once
 * (caret glide, scroll, diagnostic focus reveal, welcome overlay fade) by
 * construction, since they all multiply by it. See docs/adr/0015,
 * docs/adr/0027, docs/adr/0043.
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

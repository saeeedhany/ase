#ifndef ASE_FUZZY_MATCH_H
#define ASE_FUZZY_MATCH_H

#include <QString>

/*
 * Subsequence matching with a score, for Ctrl+P — see docs/adr/0065.
 *
 * `needle` matches `haystack` if every character of the needle appears
 * in the haystack, in order, not necessarily adjacent — so `edvim`
 * finds `gui/src/editor_viewport_vim.cpp`. Case-insensitive.
 *
 * The score is what makes it useful rather than merely correct: with a
 * few hundred files, half of them match any short query, and the whole
 * job is putting the one you meant first. Higher is better; the value
 * is meaningless in isolation and only ever compared against other
 * candidates for the same needle.
 *
 * What it rewards, in descending order of how much it matters:
 *
 *  - **Runs.** Consecutive matched characters score far more than
 *    scattered ones, so `vimcpp` prefers `..._vim.cpp` over a file that
 *    merely contains those letters somewhere.
 *  - **Boundaries.** A character matched right after `/`, `_`, `-`, `.`
 *    or at a camelCase hump is where a human would start typing, so it
 *    scores like the start of a word.
 *  - **The basename.** You search for a *file*, not a path; a match in
 *    the last segment beats the same match in a directory name.
 *  - **Brevity.** All else equal, the shorter path wins — a tiebreak,
 *    not a factor, so it never overrides a genuinely better match.
 *
 * Matching is greedy left-to-right: each needle character takes the
 * next haystack occurrence rather than the best one. That is a real
 * limitation — a query whose letters appear early in a bad position and
 * again later in a good one is scored on the early one — and it is
 * chosen deliberately, because the alternative is a full
 * dynamic-programming pass over every candidate on every keystroke, and
 * this editor's whole claim is that it does not do that sort of thing.
 */
namespace fuzzy {

constexpr int kRunBonus = 12;      /* per character in a consecutive run */
constexpr int kBoundaryBonus = 10; /* matched right after a separator, or a camel hump */
constexpr int kBasenameBonus = 6;  /* matched in the last path segment */
constexpr int kLeadingPenalty = 1; /* per haystack character skipped before the first match */
constexpr int kLengthPenalty = 1;  /* per haystack character overall, as a tiebreak */

inline bool isBoundaryBefore(const QString &haystack, int index) {
    if (index == 0) {
        return true;
    }
    QChar previous = haystack.at(index - 1);
    if (previous == QLatin1Char('/') || previous == QLatin1Char('_') || previous == QLatin1Char('-') ||
        previous == QLatin1Char('.') || previous == QLatin1Char(' ')) {
        return true;
    }
    /* camelCase hump: a capital preceded by a lowercase. */
    return previous.isLower() && haystack.at(index).isUpper();
}

/*
 * Returns false if `needle` is not a subsequence of `haystack`. An empty
 * needle matches everything with score 0, which is what makes an
 * unfiltered list fall back to whatever order it came in.
 */
inline bool match(const QString &needle, const QString &haystack, int *outScore) {
    int score = -haystack.size() * kLengthPenalty;
    if (needle.isEmpty()) {
        if (outScore != nullptr) {
            *outScore = score;
        }
        return true;
    }

    int basenameStart = haystack.lastIndexOf(QLatin1Char('/')) + 1;
    int haystackIndex = 0;
    int previousMatch = -2;

    for (int i = 0; i < needle.size(); ++i) {
        QChar wanted = needle.at(i).toLower();
        while (haystackIndex < haystack.size() && haystack.at(haystackIndex).toLower() != wanted) {
            haystackIndex++;
        }
        if (haystackIndex >= haystack.size()) {
            return false;
        }

        if (i == 0) {
            score -= haystackIndex * kLeadingPenalty;
        }
        if (haystackIndex == previousMatch + 1) {
            score += kRunBonus;
        }
        if (isBoundaryBefore(haystack, haystackIndex)) {
            score += kBoundaryBonus;
        }
        if (haystackIndex >= basenameStart) {
            score += kBasenameBonus;
        }

        previousMatch = haystackIndex;
        haystackIndex++;
    }

    if (outScore != nullptr) {
        *outScore = score;
    }
    return true;
}

} // namespace fuzzy

#endif /* ASE_FUZZY_MATCH_H */

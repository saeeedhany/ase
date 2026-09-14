#ifndef ASE_FUZZY_MATCH_H
#define ASE_FUZZY_MATCH_H

#include <QString>

/*
 * Case-insensitive subsequence matching with a score, for Ctrl+P: every
 * needle character appears in order, not necessarily adjacent, so
 * `edvim` finds `editor_viewport_vim.cpp`. See docs/adr/0065.
 *
 * Higher is better, and meaningless except against other candidates for
 * the same needle. It rewards, in descending order: runs of consecutive
 * matches; characters at a boundary (`/` `_` `-` `.` or a camelCase
 * hump); matches in the basename over a directory; and, as a tiebreak
 * only, brevity.
 *
 * Greedy left-to-right — each needle character takes the next
 * occurrence, not the best one. A real limitation, accepted because the
 * alternative is a dynamic-programming pass per candidate per
 * keystroke.
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

/* An empty needle matches everything at score 0, so an unfiltered list
 * keeps the order it came in. */
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

#ifndef ASE_VIM_PENDING_H
#define ASE_VIM_PENDING_H

#include <algorithm>

/* The half-typed command: everything entered since the last one
 * resolved, and nothing that outlives it. `dd` parks the operator here
 * between the two keys; `2d3w` parks both counts; `"a`, `m`, `q`, `f`
 * and `r` each park the letter they are waiting on.
 *
 * It is one struct because the fields are cleared together, and they
 * are cleared often — 36 call sites. Listing them by hand is how a
 * field added later gets left behind, so reset() assigns a fresh value
 * instead and a new member is covered the moment it is declared. */
struct VimPending {
    int count1 = 0;              /* typed before the operator */
    int count2 = 0;              /* typed after it: the 3 in d3w */
    char op = '\0';              /* d y c > <, or '\0' */
    bool g = false;              /* mid-"gg" */
    char find = '\0';            /* f F t T, awaiting its target */
    bool replace = false;        /* mid-`r`, awaiting the new character */
    char mark = '\0';            /* ` or ', awaiting the mark's name */
    char macro = '\0';           /* q or @, awaiting the register */
    char textObject = '\0';      /* i or a, awaiting the object */
    char registerName = '\0';    /* the x in "x */
    bool awaitingRegister = false;

    void reset() { *this = VimPending(); }

    /* vim multiplies the two counts: 2d3w deletes six words. An unset
     * count reads as one. */
    int count() const { return std::max(1, count1) * std::max(1, count2); }

    bool idle() const {
        return count1 == 0 && count2 == 0 && op == '\0' && !g && find == '\0' && !replace &&
               mark == '\0' && macro == '\0' && textObject == '\0' && registerName == '\0' &&
               !awaitingRegister;
    }
};

#endif

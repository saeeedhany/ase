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

    /*
     * True when the next key is an argument rather than a command: the
     * target of `f`, the replacement for `r`, a mark's name, a
     * register, the object after `i`/`a`, the second half of `g`.
     *
     * A remap must not touch those. Remapping the target of `f` would
     * make it unable to find a character you had rebound, and `"ayy`
     * would yank into whatever register `a` maps to.
     *
     * A pending *operator* is deliberately absent: after `d` the next
     * key is a motion, and a remapped motion composing with a pending
     * operator is the whole point. See docs/adr/0134.
     */
    bool expectsArgument() const {
        return find != '\0' || replace || mark != '\0' || macro != '\0' ||
               textObject != '\0' || awaitingRegister || g;
    }

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

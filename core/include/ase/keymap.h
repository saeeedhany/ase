#ifndef ASE_KEYMAP_H
#define ASE_KEYMAP_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Chords, as text, in one spelling.
 *
 * A user writes `Ctrl+Shift+F`, `shift+ctrl+f` or `CTRL+SHIFT+F` and
 * means the same thing; the editor has to agree with all three before it
 * can look one up. Canonicalising to a single form makes the lookup a
 * string compare and the config file's keys the same thing the editor
 * builds from a key press.
 *
 * The form is: modifiers in the order ctrl, alt, shift, meta, then the
 * key, all lowercase, joined with '+'.
 */

/* Writes the canonical spelling of `text` into `out`.
 *
 * False when the chord names no key this editor knows, when a modifier
 * is repeated, or when there is no key at all — so a typo in a config
 * file can be reported rather than silently binding nothing. */
bool ase_keymap_canonical(const char *text, char *out, size_t out_size);

/* The same, from parts: what the GUI has after a key press. `key` is a
 * key name without modifiers ("f", "f1", "escape"). */
bool ase_keymap_format(bool ctrl, bool alt, bool shift, bool meta, const char *key, char *out,
                       size_t out_size);

/* True when `name` is a key this editor can name in a binding. */
bool ase_keymap_is_known_key(const char *name);

#ifdef __cplusplus
}
#endif

#endif

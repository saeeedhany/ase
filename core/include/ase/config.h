#ifndef ASE_CONFIG_H
#define ASE_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Minimal key=value config/theme store. See
 * docs/adr/0008-config-theme-format.md for why this isn't TOML, why
 * config and theme share one file, and how the default text color is
 * meant to be tuned (edit + hot-reload, not a hardcoded guess).
 */

typedef struct AseConfig AseConfig;

/* The shipped defaults (background/text colors, font family/size) —
 * see docs/adr/0008, decision 3, for the exact values and why. */
AseConfig *ase_config_create_default(void);

/* Defaults, overlaid with whatever `path` sets. A missing or
 * unparseable file is not an error — you get defaults back, never
 * NULL (except on allocation failure). */
AseConfig *ase_config_load(const char *path);

void ase_config_destroy(AseConfig *config);

/* NULL if `key` was never set (by defaults or the loaded file). */
const char *ase_config_get_string(const AseConfig *config, const char *key);

/* `fallback` if `key` is missing or not a valid integer. */
long ase_config_get_int(const AseConfig *config, const char *key, long fallback);

/* Parses a "#RRGGBB" or "#RRGGBBAA" value. Returns false (leaving the
 * out-params untouched) if `key` is missing or malformed. */
bool ase_config_get_color(const AseConfig *config, const char *key,
                           uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a);

/* The language a file is written in, e.g. "c" or "cpp" — this is also
 * the LSP languageId. Resolved from the suffix against a built-in
 * table, which `filetype.<suffix> = <language>` overrides. NULL if the
 * suffix is unknown. Valid until `config` is destroyed. */
const char *ase_config_language_for_path(const AseConfig *config, const char *path);

/* A per-language setting: `lang.<language>.<key>`. NULL if unset, so
 * callers can fall back to a global key of their own choosing. */
const char *ase_config_get_lang_string(const AseConfig *config, const char *language,
                                        const char *key);

/* This platform's config file path
 * ($XDG_CONFIG_HOME or ~/.config on Unix, %APPDATA% on Windows, then
 * "/ase/config.ase"). NULL if it can't be determined (no HOME/APPDATA).
 * Caller owns the returned string (free()). */
char *ase_config_default_path(void);

/* Best-effort: if `path` doesn't already exist, writes a commented
 * starter file there (creating only the immediate parent directory, not
 * any missing grandparent — see docs/adr/0008, decision 5). Returns
 * true if the file exists afterward (already there, or just written);
 * false only means "no starter file written," never a hard failure the
 * caller needs to handle. */
bool ase_config_write_default_if_missing(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* ASE_CONFIG_H */

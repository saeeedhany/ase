#ifndef ASE_JSON_H
#define ASE_JSON_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Minimal JSON value tree — parser, writer, and a builder API. Scoped
 * to exactly what the LSP client (core/include/ase/lsp_client.h) needs
 * to speak JSON-RPC, not general-purpose JSON edge-case coverage. See
 * docs/adr/0010-hand-rolled-json.md for why this isn't a vendored
 * library, following the same reasoning as ADR 0008's config format.
 */

typedef enum {
    ASE_JSON_NULL,
    ASE_JSON_BOOL,
    ASE_JSON_NUMBER,
    ASE_JSON_STRING,
    ASE_JSON_ARRAY,
    ASE_JSON_OBJECT,
} AseJsonType;

typedef struct AseJsonValue AseJsonValue;

/* --- Construction: build a value tree to send. Ownership of any value
 * passed to _append/_set transfers to the container — don't destroy it
 * separately, and don't reuse it in a second container. --- */

AseJsonValue *ase_json_null(void);
AseJsonValue *ase_json_bool(bool value);
AseJsonValue *ase_json_number(double value);
AseJsonValue *ase_json_string(const char *value); /* copies */
AseJsonValue *ase_json_array(void);
AseJsonValue *ase_json_object(void);

/* Takes ownership of `item`/`value` on success. On failure (allocation),
 * the item/value is destroyed and false is returned — never leaked,
 * never double-owned. */
bool ase_json_array_append(AseJsonValue *array, AseJsonValue *item);
bool ase_json_object_set(AseJsonValue *object, const char *key, AseJsonValue *value);

/* Recursively frees. Safe on NULL. */
void ase_json_destroy(AseJsonValue *value);

/* --- Inspection: everything here returns borrowed pointers/values —
 * never destroy the result of _array_get / _object_get. --- */

AseJsonType ase_json_type(const AseJsonValue *value);
bool ase_json_get_bool(const AseJsonValue *value, bool fallback);
double ase_json_get_number(const AseJsonValue *value, double fallback);
const char *ase_json_get_string(const AseJsonValue *value); /* NULL if not a string */
size_t ase_json_array_size(const AseJsonValue *array); /* 0 if not an array */
AseJsonValue *ase_json_array_get(const AseJsonValue *array, size_t index); /* NULL if out of range */
AseJsonValue *ase_json_object_get(const AseJsonValue *object, const char *key); /* NULL if absent */

/* --- Parse / serialize --- */

/* NULL on malformed input. `len` need not include a NUL terminator. */
AseJsonValue *ase_json_parse(const char *text, size_t len);

/* Malloc'd, caller frees. NULL on allocation failure. */
char *ase_json_write(const AseJsonValue *value);

#ifdef __cplusplus
}
#endif

#endif /* ASE_JSON_H */

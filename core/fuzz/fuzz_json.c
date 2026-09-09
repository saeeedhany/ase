/*
 * libFuzzer harness for the JSON parser — the exact boundary that will
 * handle untrusted bytes from an external language server process
 * (spec section 5: "a misbehaving language server must never crash the
 * editor").
 *
 * Build (clang only): cmake -B build -DASE_BUILD_FUZZERS=ON -DCMAKE_C_COMPILER=clang
 * Run:                ./build/core/fuzz/ase_core_fuzz_json
 */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "ase/json.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    AseJsonValue *value = ase_json_parse((const char *)data, size);
    if (value != NULL) {
        char *text = ase_json_write(value);
        if (text != NULL) {
            free(text);
        }
        ase_json_destroy(value);
    }
    return 0;
}

/*
 * libFuzzer harness for the piece-table buffer engine. Interprets the
 * input as a script of insert/delete ops applied to a fresh buffer;
 * relies on ase_buffer_insert/_delete's own bounds-checking to reject
 * invalid offsets safely — the goal is to find crashes/UB under
 * ASan/UBSan, not to construct semantically valid edit scripts.
 *
 * Build (clang only): cmake -B build -DASE_BUILD_FUZZERS=ON -DCMAKE_C_COMPILER=clang
 * Run:                ./build/core/fuzz/ase_core_fuzz_buffer
 */

#include <stddef.h>
#include <stdint.h>

#include "ase/buffer.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 2) {
        return 0;
    }

    AseBuffer *buf = ase_buffer_create();
    if (buf == NULL) {
        return 0;
    }

    size_t pos = 0;
    while (pos + 2 <= size) {
        uint8_t op = data[pos++];
        uint8_t raw_len = data[pos++];
        size_t len = raw_len;
        size_t current = ase_buffer_length(buf);
        size_t at = current > 0 ? (data[pos % size] % (current + 1)) : 0;

        if (pos + len > size) {
            len = size - pos;
        }

        if (op % 2 == 0) {
            ase_buffer_insert(buf, at, (const char *)data + pos, len);
        } else {
            size_t max_delete = current > at ? current - at : 0;
            if (len > max_delete) {
                len = max_delete;
            }
            ase_buffer_delete(buf, at, len);
        }
        pos += len;
    }

    char scratch[256];
    size_t total = ase_buffer_length(buf);
    if (total > 0) {
        size_t read_len = total < sizeof(scratch) ? total : sizeof(scratch);
        ase_buffer_get_text(buf, 0, read_len, scratch);
    }

    ase_buffer_destroy(buf);
    return 0;
}

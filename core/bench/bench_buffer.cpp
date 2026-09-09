/*
 * Manual benchmark tool for the piece-table buffer engine — not run by
 * ctest (timing isn't a pass/fail signal). Written in C++ purely for
 * <chrono>'s portable wall-clock timer; the engine itself stays plain C
 * per docs/adr/0004. See docs/adr/0005 for what these numbers are meant
 * to validate.
 *
 * Build: cmake -B build -DASE_BUILD_BENCH=ON && cmake --build build --target ase_core_bench
 * Run:   ./build/core/bench/ase_core_bench
 */

#include <chrono>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

extern "C" {
#include "ase/buffer.h"
}

using Clock = std::chrono::steady_clock;

static double elapsed_ms(Clock::time_point start, Clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

static std::string make_filler_text(size_t target_bytes) {
    static const char *line = "The quick brown fox jumps over the lazy dog.\n";
    size_t line_len = std::strlen(line);
    std::string s;
    s.reserve(target_bytes + line_len);
    while (s.size() < target_bytes) {
        s.append(line, line_len);
    }
    return s;
}

static void bench_sequential_typing(AseBuffer *buf, int iterations) {
    size_t at = ase_buffer_length(buf);
    auto start = Clock::now();
    for (int i = 0; i < iterations; ++i) {
        ase_buffer_insert(buf, at, "x", 1);
        at += 1;
    }
    auto end = Clock::now();
    double ms = elapsed_ms(start, end);
    std::printf("sequential typing: %d inserts in %.2f ms (%.3f us/op, %zu pieces)\n",
                iterations, ms, (ms * 1000.0) / iterations, ase_buffer_piece_count(buf));
}

static void bench_sequential_backspace(AseBuffer *buf, int iterations) {
    auto start = Clock::now();
    for (int i = 0; i < iterations; ++i) {
        size_t len = ase_buffer_length(buf);
        if (len == 0) {
            break;
        }
        ase_buffer_delete(buf, len - 1, 1);
    }
    auto end = Clock::now();
    double ms = elapsed_ms(start, end);
    std::printf("sequential backspace: %d deletes in %.2f ms (%.3f us/op, %zu pieces)\n",
                iterations, ms, (ms * 1000.0) / iterations, ase_buffer_piece_count(buf));
}

static void bench_random_edits(AseBuffer *buf, int iterations) {
    std::mt19937_64 rng(42);
    auto start = Clock::now();
    for (int i = 0; i < iterations; ++i) {
        size_t len = ase_buffer_length(buf);
        size_t at = len ? (rng() % len) : 0;
        if (i % 2 == 0) {
            ase_buffer_insert(buf, at, "y", 1);
        } else if (len > 0) {
            ase_buffer_delete(buf, at, 1);
        }
    }
    auto end = Clock::now();
    double ms = elapsed_ms(start, end);
    std::printf("random-offset edits: %d ops in %.2f ms (%.3f us/op, %zu pieces)\n",
                iterations, ms, (ms * 1000.0) / iterations, ase_buffer_piece_count(buf));
}

static void bench_viewport_reads(AseBuffer *buf, int iterations, size_t chunk) {
    std::vector<char> out(chunk);
    size_t len = ase_buffer_length(buf);
    std::mt19937_64 rng(7);
    auto start = Clock::now();
    for (int i = 0; i < iterations; ++i) {
        size_t at = len > chunk ? (rng() % (len - chunk)) : 0;
        ase_buffer_get_text(buf, at, chunk, out.data());
    }
    auto end = Clock::now();
    double ms = elapsed_ms(start, end);
    std::printf("viewport reads: %d reads of %zu bytes in %.2f ms (%.3f us/op)\n",
                iterations, chunk, ms, (ms * 1000.0) / iterations);
}

int main() {
    const size_t seed_bytes = 50 * 1024 * 1024;
    std::string seed = make_filler_text(seed_bytes);

    AseBuffer *buf = ase_buffer_create();
    ase_buffer_insert(buf, 0, seed.data(), seed.size());
    std::printf("seeded buffer: %zu bytes, %zu pieces\n\n",
                ase_buffer_length(buf), ase_buffer_piece_count(buf));

    bench_sequential_typing(buf, 100000);
    bench_sequential_backspace(buf, 100000);
    bench_random_edits(buf, 20000);
    bench_viewport_reads(buf, 5000, 4096);

    ase_buffer_destroy(buf);
    return 0;
}

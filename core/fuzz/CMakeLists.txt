# Requires clang (libFuzzer ships with it). Not wired into the default CI
# workflow yet — see docs/ROADMAP.md and docs/adr/0005.
add_executable(ase_core_fuzz_buffer
    fuzz_buffer.c
)

target_link_libraries(ase_core_fuzz_buffer PRIVATE ase_core)

target_compile_options(ase_core_fuzz_buffer PRIVATE
    -fsanitize=fuzzer,address,undefined
    -fno-omit-frame-pointer
    -g
)
target_link_options(ase_core_fuzz_buffer PRIVATE
    -fsanitize=fuzzer,address,undefined
)

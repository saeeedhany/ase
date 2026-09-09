# Requires clang (libFuzzer ships with it). Not wired into the default CI
# workflow yet — same tracked follow-up as core/fuzz (docs/ROADMAP.md).
add_executable(ase_syntax_fuzz
    fuzz_syntax.c
)

target_link_libraries(ase_syntax_fuzz PRIVATE ase_syntax)

target_compile_options(ase_syntax_fuzz PRIVATE
    -fsanitize=fuzzer,address,undefined
    -fno-omit-frame-pointer
    -g
)
target_link_options(ase_syntax_fuzz PRIVATE
    -fsanitize=fuzzer,address,undefined
)

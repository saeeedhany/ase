add_executable(ase_core_bench
    bench_buffer.cpp
)

target_link_libraries(ase_core_bench PRIVATE ase_core)

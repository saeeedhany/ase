add_executable(ase_core_tests
    test_core.c
)
target_link_libraries(ase_core_tests PRIVATE ase_core)
add_test(NAME ase_core_smoke COMMAND ase_core_tests)

add_executable(ase_buffer_tests
    test_buffer.c
)
target_link_libraries(ase_buffer_tests PRIVATE ase_core)
add_test(NAME ase_buffer_tests COMMAND ase_buffer_tests)

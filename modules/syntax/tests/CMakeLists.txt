add_executable(ase_syntax_tests
    test_syntax.c
)
target_link_libraries(ase_syntax_tests PRIVATE ase_syntax)
add_test(NAME ase_syntax_tests COMMAND ase_syntax_tests)

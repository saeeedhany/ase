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

add_executable(ase_config_tests
    test_config.c
)
target_link_libraries(ase_config_tests PRIVATE ase_core)
add_test(NAME ase_config_tests COMMAND ase_config_tests)

add_executable(ase_json_tests
    test_json.c
)
target_link_libraries(ase_json_tests PRIVATE ase_core)
add_test(NAME ase_json_tests COMMAND ase_json_tests)

add_executable(ase_undo_tests
    test_undo.c
)
target_link_libraries(ase_undo_tests PRIVATE ase_core)
add_test(NAME ase_undo_tests COMMAND ase_undo_tests)

# POSIX-only: the collision test rewrites a snapshot in place using
# dirent.h and memmem() to find it, neither of which Windows has. The
# module itself builds and works there.
if(NOT WIN32)
    add_executable(ase_recovery_tests
        test_recovery.c
    )
    target_link_libraries(ase_recovery_tests PRIVATE ase_core)
    add_test(NAME ase_recovery_tests COMMAND ase_recovery_tests)
endif()

# POSIX-only, by the test's own design, not just ase/process.h's: it
# spawns /bin/echo, /bin/pwd, /bin/cat, /bin/sh directly and includes
# <unistd.h> for usleep() — none of which exist on Windows, where
# ase_process_spawn() is unconditionally a no-op stub anyway (see
# docs/adr/0011, decision 6, and ase/process.h's own Windows branch).
# Skipped entirely rather than stubbed like test_lsp_client.c's
# internal #ifdef _WIN32 branch does, since there'd be nothing left to
# assert on a platform where every command path it exercises is
# invalid on its face.
if(NOT WIN32)
    add_executable(ase_process_tests
        test_process.c
    )
    target_link_libraries(ase_process_tests PRIVATE ase_core)
    add_test(NAME ase_process_tests COMMAND ase_process_tests)
endif()

# Native plugin test fixture: built as a loadable module (dlopen'd by
# the test at runtime, never linked directly) into a build-dir copy of
# fixtures/ alongside the .lua fixture, so the source tree stays clean
# of build output.
set(ASE_PLUGIN_FIXTURES_DIR ${CMAKE_CURRENT_BINARY_DIR}/plugin_fixtures)

add_library(test_native_plugin MODULE fixtures/test_native_plugin.c)
target_link_libraries(test_native_plugin PRIVATE ase_core)
set_target_properties(test_native_plugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${ASE_PLUGIN_FIXTURES_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${ASE_PLUGIN_FIXTURES_DIR}
    PREFIX ""
)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/uppercase.lua
    ${ASE_PLUGIN_FIXTURES_DIR}/uppercase.lua
    COPYONLY
)

add_executable(ase_plugin_host_tests
    test_plugin_host.c
)
target_link_libraries(ase_plugin_host_tests PRIVATE ase_core)
target_compile_definitions(ase_plugin_host_tests PRIVATE
    ASE_TEST_FIXTURES_DIR="${ASE_PLUGIN_FIXTURES_DIR}"
)
add_dependencies(ase_plugin_host_tests test_native_plugin)
add_test(NAME ase_plugin_host_tests COMMAND ase_plugin_host_tests)

# Fake LSP server test fixture — a standalone black-box executable, not
# linked against ase_core (see docs/adr/0011, decision 7).
add_executable(fake_lsp_server
    fixtures/fake_lsp_server.c
)

add_executable(ase_lsp_client_tests
    test_lsp_client.c
)
target_link_libraries(ase_lsp_client_tests PRIVATE ase_core)
target_compile_definitions(ase_lsp_client_tests PRIVATE
    ASE_TEST_FAKE_LSP_SERVER="$<TARGET_FILE:fake_lsp_server>"
)
add_dependencies(ase_lsp_client_tests fake_lsp_server)
add_test(NAME ase_lsp_client_tests COMMAND ase_lsp_client_tests)

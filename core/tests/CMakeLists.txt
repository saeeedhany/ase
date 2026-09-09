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

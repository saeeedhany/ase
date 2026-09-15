find_package(Qt6 6.2 COMPONENTS Test QUIET)

# The vim layer is the largest and most intricate part of the editor and
# had no automated coverage at all — every behaviour was verified once,
# by hand, against real vim, and then forgotten. See docs/adr/0104.
if(NOT Qt6Test_FOUND)
    message(STATUS "Qt6 Test not found — skipping the vim conformance suite")
    return()
endif()

add_executable(ase_vim_tests test_vim.cpp)
target_link_libraries(ase_vim_tests PRIVATE ase_gui_objects Qt6::Test ase_warnings)

add_test(NAME ase_vim_tests COMMAND ase_vim_tests)
# Offscreen so it needs no display; a private config dir so it never
# reads or writes the developer's own settings.
set_tests_properties(ase_vim_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/config"
)

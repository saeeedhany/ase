find_package(Qt6 6.2 COMPONENTS Test QUIET)

# The vim layer is the largest and most intricate part of the editor and
# had no automated coverage at all — every behaviour was verified once,
# by hand, against real vim, and then forgotten. See docs/adr/0104.
#
# A missing Qt6::Test is fatal rather than a skip, for the same reason a
# missing Qt6::Widgets is fatal one directory up: a skip leaves CI green
# with the suite never having run, which is indistinguishable from the
# suite passing. Qt6 Test ships inside qtbase, so anything with
# Qt6::Widgets already has it; if it is genuinely absent, say so.
if(NOT Qt6Test_FOUND)
    message(FATAL_ERROR
        "Qt6 (Test) not found, but ASE_BUILD_TESTS is ON and the GUI is "
        "being built. Qt6 Test is part of qtbase — install it (Debian and "
        "Ubuntu: qt6-base-dev), or configure with -DASE_BUILD_TESTS=OFF."
    )
endif()

add_executable(ase_vim_tests test_vim.cpp)
target_link_libraries(ase_vim_tests PRIVATE ase_gui_objects Qt6::Test ase_warnings)

add_test(NAME ase_vim_tests COMMAND ase_vim_tests)
# Offscreen so it needs no display; a private config dir so it never
# reads or writes the developer's own settings.
set_tests_properties(ase_vim_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/config"
)

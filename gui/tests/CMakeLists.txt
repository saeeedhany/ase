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

add_executable(ase_recovery_flow_tests test_recovery_flow.cpp)
target_link_libraries(ase_recovery_flow_tests PRIVATE ase_gui_objects Qt6::Test ase_warnings)
add_test(NAME ase_recovery_flow_tests COMMAND ase_recovery_flow_tests)
set_tests_properties(ase_recovery_flow_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/config"
)
# Offscreen so it needs no display; a private config dir so it never
# reads or writes the developer's own settings.
set_tests_properties(ase_vim_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/config"
)

add_executable(ase_keybinding_tests test_keybindings.cpp)
target_link_libraries(ase_keybinding_tests PRIVATE ase_gui_objects Qt6::Test ase_warnings)
add_test(NAME ase_keybinding_tests COMMAND ase_keybinding_tests)
set_tests_properties(ase_keybinding_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/config"
)

add_executable(ase_plugin_flow_tests test_plugin_flow.cpp)
target_link_libraries(ase_plugin_flow_tests PRIVATE ase_gui_objects Qt6::Test ase_warnings)
add_test(NAME ase_plugin_flow_tests COMMAND ase_plugin_flow_tests)
set_tests_properties(ase_plugin_flow_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/config"
)

add_executable(ase_multifile_edit_tests test_multifile_edit.cpp)
target_link_libraries(ase_multifile_edit_tests PRIVATE ase_gui_objects Qt6::Test ase_warnings)
add_test(NAME ase_multifile_edit_tests COMMAND ase_multifile_edit_tests)
set_tests_properties(ase_multifile_edit_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/config"
)

add_executable(ase_vim_remap_tests test_vim_remap.cpp)
target_link_libraries(ase_vim_remap_tests PRIVATE ase_gui_objects Qt6::Test ase_warnings)
add_test(NAME ase_vim_remap_tests COMMAND ase_vim_remap_tests)
set_tests_properties(ase_vim_remap_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/remap-config"
)

add_executable(ase_multicursor_tests test_multicursor.cpp)
target_link_libraries(ase_multicursor_tests PRIVATE ase_gui_objects Qt6::Test ase_warnings)
add_test(NAME ase_multicursor_tests COMMAND ase_multicursor_tests)
set_tests_properties(ase_multicursor_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/config"
)

add_executable(ase_buffer_bar_tests test_buffer_bar.cpp)
target_link_libraries(ase_buffer_bar_tests PRIVATE ase_gui_objects Qt6::Test ase_warnings)
add_test(NAME ase_buffer_bar_tests COMMAND ase_buffer_bar_tests)
set_tests_properties(ase_buffer_bar_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/config"
)

add_executable(ase_selection_shape_tests test_selection_shape.cpp)
target_link_libraries(ase_selection_shape_tests PRIVATE ase_gui_objects Qt6::Test ase_warnings)
add_test(NAME ase_selection_shape_tests COMMAND ase_selection_shape_tests)
set_tests_properties(ase_selection_shape_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/config"
)

add_executable(ase_accessible_tests test_accessible.cpp)
target_link_libraries(ase_accessible_tests PRIVATE ase_gui_objects Qt6::Test ase_warnings)
add_test(NAME ase_accessible_tests COMMAND ase_accessible_tests)
set_tests_properties(ase_accessible_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/config"
)

# 6.2 is the floor, and it is not arbitrary: it is what ubuntu:22.04
# ships, which is the oldest base packaging/ builds against (see
# packaging/README.md and docs/adr/0045). Stating it here makes a build
# on something older fail at *configure* time with a version number,
# rather than deep in a compile with a missing-member error — which is
# exactly how a Qt 6.6-only QList::assign() slipped through three
# releases of native builds on a rolling-release dev machine.
find_package(Qt6 6.2 COMPONENTS Widgets QUIET)

# ASE_BUILD_GUI defaults to ON, so reaching this file at all means the
# GUI was requested (explicitly or by that default) — a missing Qt6
# here used to be a message(WARNING) + return(), which let a plain
# `cmake -B build && cmake --build build` finish "successfully" with
# no ase_gui binary and only a warning easy to miss in the configure
# log (see docs/feedback/0001, item 3). Anyone who actually wants a
# headless build already passes -DASE_BUILD_GUI=OFF explicitly (see
# README's "Headless core-only build"), so there's no legitimate case
# left where silently dropping the GUI target is the right behavior.
if(NOT Qt6_FOUND)
    message(FATAL_ERROR
        "Qt6 (Widgets) not found, but ASE_BUILD_GUI is ON. "
        "Install Qt6, or configure with -DASE_BUILD_GUI=OFF for a "
        "headless core-only build; see docs/adr/0002-headless-core-separation.md."
    )
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

add_executable(ase_gui
    src/main.cpp
    # EditorViewport is one class deliberately spread across several
    # translation units — see docs/adr/0052. They share a private
    # src/editor_viewport_internal.h; nothing else includes it.
    src/editor_viewport.cpp
    src/editor_viewport_config.cpp
    src/editor_viewport_render.cpp
    src/editor_viewport_input.cpp
    src/editor_viewport_edit.cpp
    src/editor_viewport_vim.cpp
    src/editor_viewport_find.cpp
    src/editor_viewport_commands.cpp
    src/editor_viewport_lsp.cpp
    src/buffer_bar.cpp
    src/find_bar.cpp
    src/file_browser_panel.cpp
    src/command_line.cpp
    src/output_panel.cpp
    src/project_files.cpp
    src/help_panel.cpp
    src/about_panel.cpp
    src/floating_panel.cpp
    src/letter_badge.cpp
    src/translucent_bar.cpp
    src/scrollbar_style.cpp
    src/tracking_popup.cpp
    src/completion_popup.cpp
    src/hover_panel.cpp
    src/smooth_line_edit.cpp
    src/smooth_scroll.cpp
    resources/resources.qrc
)

target_link_libraries(ase_gui
    PRIVATE
        ase_core
        ase_syntax
        Qt6::Widgets
)

# Single source of truth for the version shown in the About panel —
# the top-level project() VERSION, plus a release-stage suffix that
# isn't part of the numeric CMake version. Bump PROJECT_VERSION (and
# retag) for a real release; flip/drop "-alpha" here as the project
# matures past it.
target_compile_definitions(ase_gui PRIVATE
    ASE_VERSION_STRING="${PROJECT_VERSION}-alpha"
)

# Standard freedesktop install layout — what every packaging format in
# packaging/ (AppImage's AppDir, the Arch PKGBUILD, the .deb) actually
# runs `cmake --install` against, rather than each reimplementing its
# own copy step. See packaging/README.md.
include(GNUInstallDirs)
install(TARGETS ase_gui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${CMAKE_SOURCE_DIR}/packaging/linux/ase.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)
install(FILES ${CMAKE_SOURCE_DIR}/packaging/linux/ase-icon-512.png
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/512x512/apps
    RENAME ase.png
)

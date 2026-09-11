find_package(Qt6 COMPONENTS Widgets QUIET)

if(NOT Qt6_FOUND)
    message(WARNING
        "Qt6 not found — skipping the GUI shell target. "
        "The core engine build is unaffected; see docs/adr/0002-headless-core-separation.md."
    )
    return()
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

add_executable(ase_gui
    src/main.cpp
    src/editor_viewport.cpp
    src/find_bar.cpp
    src/file_browser_panel.cpp
    src/command_line.cpp
    src/output_panel.cpp
    src/help_panel.cpp
    src/about_panel.cpp
    src/floating_panel.cpp
    src/letter_badge.cpp
    src/translucent_bar.cpp
    src/scrollbar_style.cpp
    src/tracking_popup.cpp
    src/completion_popup.cpp
    src/hover_panel.cpp
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

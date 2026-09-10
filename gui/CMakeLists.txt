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
    resources/resources.qrc
)

target_link_libraries(ase_gui
    PRIVATE
        ase_core
        ase_syntax
        Qt6::Widgets
)

find_package(Qt6 COMPONENTS Widgets QUIET)

if(NOT Qt6_FOUND)
    message(WARNING
        "Qt6 not found — skipping the GUI shell target. "
        "The core engine build is unaffected; see docs/adr/0002-headless-core-separation.md."
    )
    return()
endif()

set(CMAKE_AUTOMOC ON)

add_executable(ase_gui
    src/main.cpp
    src/editor_viewport.cpp
    src/find_bar.cpp
)

target_link_libraries(ase_gui
    PRIVATE
        ase_core
        ase_syntax
        Qt6::Widgets
)

cmake_minimum_required(VERSION 3.20)

project(absolute_simple_editor
    VERSION 0.0.0
    LANGUAGES C CXX
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ase_core is a static library that also gets linked into dlopen'd
# plugin modules (core/tests/fixtures/test_native_plugin.c, and any real
# native plugin later) — those require -fPIC in everything they link
# against, so set it project-wide rather than per-target.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(ASE_BUILD_GUI "Build the Qt6 GUI shell" ON)
option(ASE_BUILD_TESTS "Build unit tests for the core engine" ON)
option(ASE_ENABLE_SANITIZERS "Build core with ASan/UBSan" OFF)
option(ASE_BUILD_BENCH "Build the core engine benchmark tool" OFF)
option(ASE_BUILD_FUZZERS "Build libFuzzer harnesses for the core engine (requires clang)" OFF)
option(ASE_BUILD_SYNTAX "Build the Tree-sitter syntax module (fetches deps from GitHub)" ON)

enable_testing()

if(ASE_BUILD_GUI AND NOT ASE_BUILD_SYNTAX)
    message(FATAL_ERROR
        "ASE_BUILD_GUI requires ASE_BUILD_SYNTAX (the GUI links against "
        "ase_syntax unconditionally). Either enable ASE_BUILD_SYNTAX or "
        "disable ASE_BUILD_GUI."
    )
endif()

# The core engine must build and run without the GUI dependency.
# See docs/adr/0002-headless-core-separation.md.
add_subdirectory(core)

if(ASE_BUILD_SYNTAX)
    add_subdirectory(modules/syntax)
endif()

if(ASE_BUILD_GUI)
    add_subdirectory(gui)
endif()

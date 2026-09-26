cmake_minimum_required(VERSION 3.20)

project(absolute_simple_editor
    VERSION 0.4.0
    LANGUAGES C CXX
)

# The release stage, beside the number rather than repeated next to it.
# It had been written out in three places — the About panel's definition
# and both packaging scripts — so 0.4.0-beta shipped .deb and AppImage
# artifacts still labelled alpha until this was one line. The packaging
# scripts grep for it by this exact name.
set(ASE_VERSION_STAGE "beta")

# An unset CMAKE_BUILD_TYPE means no -O flag at all, not a mild default:
# measured on a 276KB file, that is 6.8ms per keystroke against 2.3ms at
# -O3. `cmake -B build` is what everyone types, so it has to be fast.
# An explicit -DCMAKE_BUILD_TYPE still wins, which is what CI's Debug
# and sanitizer jobs pass.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
# Only single-config generators have that cache entry to decorate. Visual
# Studio is multi-config, so this ran against a variable that was never
# created and failed the configure outright — every Windows CI run since
# the default above was added. See docs/adr/0130.
if(NOT CMAKE_CONFIGURATION_TYPES)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

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

# The project builds clean at -Wall -Wextra and has all along; nothing
# was asking for it, so nothing was stopping the next warning from
# sneaking in. An INTERFACE target rather than a global add_compile_options
# so the flags reach this project's own code and not the Tree-sitter and
# Lua sources FetchContent pulls in, which are not ours to keep clean.
# See docs/adr/0072.
add_library(ase_warnings INTERFACE)
if(MSVC)
    target_compile_options(ase_warnings INTERFACE /W4)
    # C4013: "undefined; assuming extern returning int". In C that is not
    # a style note — a function declared implicitly returns int, so a
    # returned pointer is truncated to 32 bits and the program crashes on
    # a platform where the header happened not to be included. Twice now:
    # memmem (ADR 0130) and internal.h (ADR 0143). An error, everywhere.
    target_compile_options(ase_warnings INTERFACE $<$<COMPILE_LANGUAGE:C>:/we4013>)
else()
    target_compile_options(ase_warnings INTERFACE -Wall -Wextra
        $<$<COMPILE_LANGUAGE:C>:-Werror=implicit-function-declaration>)
endif()

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

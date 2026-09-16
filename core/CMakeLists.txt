include(FetchContent)

# Pinned to 5.4.7, not the newer 5.5.x line — see docs/adr/0009, decision 2.
# No CMakeLists.txt upstream (confirmed: this is a plain FetchContent
# source pull, not an add_subdirectory of someone else's build).
FetchContent_Declare(lua
    GIT_REPOSITORY https://github.com/lua/lua.git
    GIT_TAG v5.4.7
)
FetchContent_MakeAvailable(lua)

file(GLOB ASE_LUA_SOURCES ${lua_SOURCE_DIR}/*.c)
list(REMOVE_ITEM ASE_LUA_SOURCES
    ${lua_SOURCE_DIR}/lua.c     # standalone interpreter's main() — not part of the library
    ${lua_SOURCE_DIR}/luac.c    # standalone compiler's main()
    ${lua_SOURCE_DIR}/ltests.c  # Lua's internal test harness, needs build hooks we don't set
    ${lua_SOURCE_DIR}/onelua.c  # alternative single-TU amalgamation of everything above —
                                # redundant (and would multiply-define every symbol) alongside
                                # the individual files we're already compiling
)

add_library(lua_runtime STATIC ${ASE_LUA_SOURCES})
target_include_directories(lua_runtime PUBLIC ${lua_SOURCE_DIR})
set_target_properties(lua_runtime PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
)
if(NOT WIN32)
    target_link_libraries(lua_runtime PUBLIC m)
    # Without this, loslib.c's os.tmpname() falls back to Lua's generic
    # ISO C path (tmpnam()), which every compiler warns on as unsafe.
    # Lua's own POSIX path (mkstemp()-based) is available and safe, but
    # opt-in — it's off by default in a bare FetchContent source pull
    # with no Makefile driving it. The project is already POSIX-only
    # for Lua (see docs/feedback/0001, item 7), so there's no
    # portability tradeoff to weigh here.
    target_compile_definitions(lua_runtime PRIVATE LUA_USE_POSIX)
endif()

add_library(ase_core
    src/core.c
    src/internal.c
    src/buffer.c
    src/config.c
    src/plugin_host.c
    src/json.c
    src/process.c
    src/lsp_client.c
    src/undo.c
    src/recovery.c
    src/session.c
    src/vcs.c
    src/keymap.c
)

target_include_directories(ase_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ase_core PRIVATE lua_runtime ${CMAKE_DL_LIBS}
        ase_warnings
)

if(ASE_ENABLE_SANITIZERS)
    target_compile_options(ase_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer -g)
    target_link_options(ase_core PUBLIC -fsanitize=address,undefined)
    target_compile_options(lua_runtime PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer -g)
    target_link_options(lua_runtime PUBLIC -fsanitize=address,undefined)
endif()

if(ASE_BUILD_TESTS)
    add_subdirectory(tests)
endif()

if(ASE_BUILD_BENCH)
    add_subdirectory(bench)
endif()

if(ASE_BUILD_FUZZERS)
    add_subdirectory(fuzz)
endif()

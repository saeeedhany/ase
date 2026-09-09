add_library(ase_core
    src/core.c
    src/buffer.c
)

target_include_directories(ase_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(ASE_ENABLE_SANITIZERS)
    target_compile_options(ase_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer -g)
    target_link_options(ase_core PUBLIC -fsanitize=address,undefined)
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

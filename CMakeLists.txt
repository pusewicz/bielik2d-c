cmake_minimum_required(VERSION 3.28)

project(bielik2d LANGUAGES C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(CMAKE_C_COMPILER_ID STREQUAL "MSVC")
    message(FATAL_ERROR "MSVC is not supported. Use clang-cl instead.")
endif()

option(BK_BUILD_SAMPLES "Build sample applications" ON)
option(BK_BUILD_TESTS "Build tests" ON)

include(cmake/warnings.cmake)
include(cmake/shaders.cmake)
include(cmake/format.cmake)

include(FetchContent)

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    SDL3
    URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.8/SDL3-3.4.8.zip
)
FetchContent_MakeAvailable(SDL3)

add_library(bielik STATIC src/bk_app.c src/bk_draw.c src/bk_gfx.c src/bk_gfx_buffer.c src/bk_gfx_canvas.c src/bk_gfx_pipeline.c src/bk_gfx_texture.c src/bk_task.c src/bk_time.c)
add_library(Bielik2D::bielik ALIAS bielik)

target_include_directories(bielik PUBLIC include)
target_link_libraries(bielik PUBLIC SDL3::SDL3)
target_compile_features(bielik PUBLIC c_std_23)
target_link_libraries(bielik PRIVATE bk_warnings)

# Pin the assertion level rather than letting SDL infer it. SDL_assert.h derives its
# default from __OPTIMIZE__ (never from NDEBUG), so an unpinned Debug build that adds
# -O1/-O2 silently drops every BK_ASSERT while leaving bk_math's libc assert live.
# PUBLIC, not PRIVATE: BK_ASSERT lives in a public header, so samples, tests and any
# consumer must compile at the same level or they disagree about what's checked.
# The generator expression is required, not stylistic -- CI's Windows leg uses a
# multi-config generator, where CMAKE_BUILD_TYPE is empty at configure time.
# SDL_assert.h #errors on a value outside 0..3, so a typo fails the build loudly.
set(BK_ASSERT_LEVEL "" CACHE STRING
    "SDL assertion level: 0=off, 1=release, 2=debug, 3=paranoid. Empty = 2 in Debug, 1 otherwise.")
if(BK_ASSERT_LEVEL STREQUAL "")
    target_compile_definitions(bielik PUBLIC SDL_ASSERT_LEVEL=$<IF:$<CONFIG:Debug>,2,1>)
else()
    target_compile_definitions(bielik PUBLIC SDL_ASSERT_LEVEL=${BK_ASSERT_LEVEL})
endif()

bk_compile_shader(NAME triangle STAGE vertex)
bk_compile_shader(NAME triangle STAGE fragment)
bk_compile_shader(NAME textured STAGE vertex)
bk_compile_shader(NAME textured STAGE fragment)
bk_compile_shader(NAME gradient STAGE compute)
bk_compile_shader(NAME depth_tri STAGE vertex)
bk_compile_shader(NAME depth_tri STAGE fragment)
# Declares both a storage buffer and a uniform buffer in the vertex stage, so its MSL
# needs the binding remap -- see bk_compile_shader's comment in cmake/shaders.cmake.
bk_compile_shader(NAME instanced STAGE vertex MSL_DECORATION_BINDING)
bk_compile_shader(NAME instanced STAGE fragment)
# Declares both storage buffers and a uniform buffer (the batch-base index) in the
# vertex stage, so its MSL needs the binding remap -- see bk_compile_shader's comment
# in cmake/shaders.cmake, and shaders/instanced.vert for the existing precedent.
bk_compile_shader(NAME draw STAGE vertex MSL_DECORATION_BINDING)
bk_compile_shader(NAME draw STAGE fragment)

bk_embed_shader(NAME draw)
add_dependencies(bielik bk_embed_draw)
target_include_directories(bielik PUBLIC "${CMAKE_BINARY_DIR}/generated")

if(BK_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(BK_BUILD_SAMPLES)
    add_subdirectory(samples)
endif()

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

add_library(bielik STATIC src/bk_app.c src/bk_gfx.c src/bk_gfx_buffer.c src/bk_gfx_canvas.c src/bk_gfx_pipeline.c src/bk_gfx_texture.c src/bk_task.c src/bk_time.c)
add_library(Bielik2D::bielik ALIAS bielik)

target_include_directories(bielik PUBLIC include)
target_link_libraries(bielik PUBLIC SDL3::SDL3)
target_compile_features(bielik PUBLIC c_std_23)
target_link_libraries(bielik PRIVATE bk_warnings)

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

if(BK_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(BK_BUILD_SAMPLES)
    add_subdirectory(samples)
endif()

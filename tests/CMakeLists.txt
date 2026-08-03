add_executable(test_version test_version.c)
target_link_libraries(test_version PRIVATE bielik bk_warnings)
add_test(NAME test_version COMMAND test_version)

add_executable(test_time test_time.c)
target_link_libraries(test_time PRIVATE bielik bk_warnings)
add_test(NAME test_time COMMAND test_time)

add_executable(test_task test_task.c)
target_link_libraries(test_task PRIVATE bielik bk_warnings)
target_include_directories(test_task PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(NAME test_task COMMAND test_task)

add_executable(test_arena test_arena.c)
target_link_libraries(test_arena PRIVATE bielik bk_warnings)
target_include_directories(test_arena PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(NAME test_arena COMMAND test_arena)

add_executable(test_math test_math.c)
target_link_libraries(test_math PRIVATE bielik bk_warnings)
add_test(NAME test_math COMMAND test_math)

add_executable(test_gfx test_gfx.c)
target_link_libraries(test_gfx PRIVATE bielik bk_warnings)
target_include_directories(test_gfx PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(NAME test_gfx COMMAND test_gfx)

add_executable(test_app_lifecycle test_app_lifecycle.c)
target_link_libraries(test_app_lifecycle PRIVATE bielik bk_warnings)
add_test(NAME test_app_lifecycle COMMAND test_app_lifecycle)
set_tests_properties(test_app_lifecycle PROPERTIES TIMEOUT 30)

add_executable(test_app_init_failure test_app_init_failure.c)
target_link_libraries(test_app_init_failure PRIVATE bielik bk_warnings)
add_test(NAME test_app_init_failure COMMAND test_app_init_failure)
set_tests_properties(test_app_init_failure PROPERTIES TIMEOUT 30)

add_library(test_header_bk_app OBJECT test_header_bk_app.c)
target_link_libraries(test_header_bk_app PRIVATE bielik bk_warnings)

add_library(test_header_bk_main OBJECT test_header_bk_main.c)
target_link_libraries(test_header_bk_main PRIVATE bielik bk_warnings)

add_library(test_header_bk_time OBJECT test_header_bk_time.c)
target_link_libraries(test_header_bk_time PRIVATE bielik bk_warnings)

add_library(test_header_bk_types OBJECT test_header_bk_types.c)
target_link_libraries(test_header_bk_types PRIVATE bielik bk_warnings)

add_library(test_header_bk_math OBJECT test_header_bk_math.c)
target_link_libraries(test_header_bk_math PRIVATE bielik bk_warnings)

add_library(test_header_bk_draw OBJECT test_header_bk_draw.c)
target_link_libraries(test_header_bk_draw PRIVATE bielik bk_warnings)

add_library(test_header_bk_task OBJECT test_header_bk_task.c)
target_link_libraries(test_header_bk_task PRIVATE bielik bk_warnings)

add_library(test_header_bk_gfx OBJECT test_header_bk_gfx.c)
target_link_libraries(test_header_bk_gfx PRIVATE bielik bk_warnings)

add_executable(test_gfx_pipeline test_gfx_pipeline.c)
target_link_libraries(test_gfx_pipeline PRIVATE bielik bk_warnings)
target_include_directories(test_gfx_pipeline PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(NAME test_gfx_pipeline COMMAND test_gfx_pipeline)
bk_stage_shaders(test_gfx_pipeline)

add_library(test_header_bk_gfx_pipeline OBJECT test_header_bk_gfx_pipeline.c)
target_link_libraries(test_header_bk_gfx_pipeline PRIVATE bielik bk_warnings)

add_executable(test_gfx_capture test_gfx_capture.c)
target_link_libraries(test_gfx_capture PRIVATE bielik bk_warnings)
add_test(NAME test_gfx_capture COMMAND test_gfx_capture)
set_tests_properties(test_gfx_capture PROPERTIES TIMEOUT 30)

add_executable(test_gfx_buffer test_gfx_buffer.c)
target_link_libraries(test_gfx_buffer PRIVATE bielik bk_warnings)
target_include_directories(test_gfx_buffer PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(NAME test_gfx_buffer COMMAND test_gfx_buffer)

add_library(test_header_bk_gfx_buffer OBJECT test_header_bk_gfx_buffer.c)
target_link_libraries(test_header_bk_gfx_buffer PRIVATE bielik bk_warnings)

add_executable(test_gfx_texture test_gfx_texture.c)
target_link_libraries(test_gfx_texture PRIVATE bielik bk_warnings)
target_include_directories(test_gfx_texture PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(NAME test_gfx_texture COMMAND test_gfx_texture)
bk_stage_shaders(test_gfx_texture)

add_library(test_header_bk_gfx_texture OBJECT test_header_bk_gfx_texture.c)
target_link_libraries(test_header_bk_gfx_texture PRIVATE bielik bk_warnings)

add_executable(test_gfx_compute test_gfx_compute.c)
target_link_libraries(test_gfx_compute PRIVATE bielik bk_warnings)
target_include_directories(test_gfx_compute PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(NAME test_gfx_compute COMMAND test_gfx_compute)
bk_stage_shaders(test_gfx_compute)

add_executable(test_gfx_canvas test_gfx_canvas.c)
target_link_libraries(test_gfx_canvas PRIVATE bielik bk_warnings)
target_include_directories(test_gfx_canvas PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(NAME test_gfx_canvas COMMAND test_gfx_canvas)
bk_stage_shaders(test_gfx_canvas)

add_library(test_header_bk_gfx_canvas OBJECT test_header_bk_gfx_canvas.c)
target_link_libraries(test_header_bk_gfx_canvas PRIVATE bielik bk_warnings)

add_executable(test_gfx_resize test_gfx_resize.c)
target_link_libraries(test_gfx_resize PRIVATE bielik bk_warnings)
target_include_directories(test_gfx_resize PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(NAME test_gfx_resize COMMAND test_gfx_resize)
set_tests_properties(test_gfx_resize PROPERTIES TIMEOUT 30)

# Record-structure tests: no GPU, so this one is a required CI check everywhere.
add_executable(test_gfx_drawlist test_gfx_drawlist.c)
target_link_libraries(test_gfx_drawlist PRIVATE bielik bk_warnings)
target_include_directories(test_gfx_drawlist PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(NAME test_gfx_drawlist COMMAND test_gfx_drawlist)

# GPU replay tests: needs a real device and pipeline, so it joins CI's allow-failure
# GPU group (no DXIL variant exists for Windows -- see DEVIATIONS.md).
add_executable(test_gfx_drawlist_gpu test_gfx_drawlist_gpu.c)
target_link_libraries(test_gfx_drawlist_gpu PRIVATE bielik bk_warnings)
target_include_directories(test_gfx_drawlist_gpu PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(NAME test_gfx_drawlist_gpu COMMAND test_gfx_drawlist_gpu)
set_tests_properties(test_gfx_drawlist_gpu PROPERTIES TIMEOUT 60)
bk_stage_shaders(test_gfx_drawlist_gpu)

# Record-structure tests: no GPU, so this one is a required CI check everywhere.
add_executable(test_draw test_draw.c)
target_link_libraries(test_draw PRIVATE bielik bk_warnings)
target_include_directories(test_draw PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(NAME test_draw COMMAND test_draw)

# GPU replay tests: needs a real device and pipeline, so it joins CI's allow-failure
# GPU group (no DXIL variant exists for Windows -- see DEVIATIONS.md).
add_executable(test_draw_gpu test_draw_gpu.c)
target_link_libraries(test_draw_gpu PRIVATE bielik bk_warnings)
target_include_directories(test_draw_gpu PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(NAME test_draw_gpu COMMAND test_draw_gpu)
set_tests_properties(test_draw_gpu PROPERTIES TIMEOUT 60)

# Every test binary here uses bk_test.h, whose REQUIRE macros are built on SDL_test's
# assertion helpers. Linked via the directory's target list rather than named on each
# target, so a test added above doesn't have to remember. The header-hygiene OBJECT
# libraries and the shader-staging custom targets are filtered out by the EXECUTABLE
# check -- they never include bk_test.h.
get_property(bk_test_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(bk_target IN LISTS bk_test_targets)
    get_target_property(bk_target_type ${bk_target} TYPE)
    if(bk_target_type STREQUAL "EXECUTABLE")
        target_link_libraries(${bk_target} PRIVATE SDL3::SDL3_test)
    endif()
endforeach()

# Two SDL env-var knobs for every test, both SDL's own mechanisms rather than
# anything hand-rolled:
#
# SDL_ASSERT=abort -- BK_ASSERT is live in Release now (see BK_ASSERT_LEVEL in the
#   top-level CMakeLists), so every test here can trip one, and SDL's default
#   assertion handler pops a modal GUI dialog. Nothing dismisses it under CTest, so
#   the test would hang until its timeout rather than failing. "abort" turns a tripped
#   assert into a non-zero exit and a normal test failure.
#
# SDL_LOGGING=test=error -- bk_test.h's REQUIRE logs through SDLTest, which logs
#   passes at INFO and failures at ERROR, and the "test" category defaults to
#   verbose. Left alone, the suite's ~580 passing assertions would each print a line
#   and bury the one that matters. Failures still print, as does the assert summary.
#   Run a test binary directly (no CTest, no env) to get the full per-assertion log
#   back when debugging one.
#
# Applied via the directory's TESTS property rather than named per test, so a test
# added above is covered without remembering to opt in. Keep this LAST in the file:
# it only sees tests registered before it runs.
get_property(bk_test_names DIRECTORY PROPERTY TESTS)
set_tests_properties(${bk_test_names} PROPERTIES
    ENVIRONMENT "SDL_ASSERT=abort;SDL_LOGGING=test=error")

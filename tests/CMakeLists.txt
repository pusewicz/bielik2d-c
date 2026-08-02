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

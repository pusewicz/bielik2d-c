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

add_executable(test_gfx test_gfx.c)
target_link_libraries(test_gfx PRIVATE bielik bk_warnings)
target_include_directories(test_gfx PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(NAME test_gfx COMMAND test_gfx)

add_executable(test_app_lifecycle test_app_lifecycle.c)
target_link_libraries(test_app_lifecycle PRIVATE bielik bk_warnings)
add_test(NAME test_app_lifecycle COMMAND test_app_lifecycle)
set_tests_properties(test_app_lifecycle PROPERTIES TIMEOUT 30)

add_library(test_header_bk_app OBJECT test_header_bk_app.c)
target_link_libraries(test_header_bk_app PRIVATE bielik bk_warnings)

add_library(test_header_bk_main OBJECT test_header_bk_main.c)
target_link_libraries(test_header_bk_main PRIVATE bielik bk_warnings)

add_library(test_header_bk_time OBJECT test_header_bk_time.c)
target_link_libraries(test_header_bk_time PRIVATE bielik bk_warnings)

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

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

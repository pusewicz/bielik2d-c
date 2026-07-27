add_executable(test_version test_version.c)
target_link_libraries(test_version PRIVATE bielik bk_warnings)
add_test(NAME test_version COMMAND test_version)

add_executable(test_time test_time.c)
target_link_libraries(test_time PRIVATE bielik bk_warnings)
add_test(NAME test_time COMMAND test_time)

add_executable(test_task test_task.c)
target_link_libraries(test_task PRIVATE bielik bk_warnings)
target_include_directories(test_task PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME test_task COMMAND test_task)

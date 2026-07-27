add_executable(test_version test_version.c)
target_link_libraries(test_version PRIVATE bielik)
add_test(NAME test_version COMMAND test_version)

add_executable(test_time test_time.c)
target_link_libraries(test_time PRIVATE bielik)
add_test(NAME test_time COMMAND test_time)

add_executable(01_clear main.c)
target_link_libraries(01_clear PRIVATE bielik)

add_executable(01_clear_run main.c)
target_link_libraries(01_clear_run PRIVATE bielik)
target_compile_definitions(01_clear_run PRIVATE BK_MAIN_HANDLED)

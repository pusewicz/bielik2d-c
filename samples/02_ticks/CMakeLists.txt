add_executable(02_ticks main.c)
target_link_libraries(02_ticks PRIVATE bielik bk_warnings)

add_executable(02_ticks_run main.c)
target_link_libraries(02_ticks_run PRIVATE bielik bk_warnings)
target_compile_definitions(02_ticks_run PRIVATE BK_MAIN_HANDLED)

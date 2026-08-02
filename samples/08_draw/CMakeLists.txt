add_executable(08_draw main.c)
target_link_libraries(08_draw PRIVATE bielik bk_warnings)

add_executable(08_draw_run main.c)
target_link_libraries(08_draw_run PRIVATE bielik bk_warnings)
target_compile_definitions(08_draw_run PRIVATE BK_MAIN_HANDLED)

add_executable(06_canvas main.c)
target_link_libraries(06_canvas PRIVATE bielik bk_warnings)
bk_stage_shaders(06_canvas)

add_executable(06_canvas_run main.c)
target_link_libraries(06_canvas_run PRIVATE bielik bk_warnings)
target_compile_definitions(06_canvas_run PRIVATE BK_MAIN_HANDLED)
bk_stage_shaders(06_canvas_run)

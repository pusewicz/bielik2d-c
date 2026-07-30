add_executable(03_triangle main.c)
target_link_libraries(03_triangle PRIVATE bielik bk_warnings)
bk_stage_shaders(03_triangle)

add_executable(03_triangle_run main.c)
target_link_libraries(03_triangle_run PRIVATE bielik bk_warnings)
target_compile_definitions(03_triangle_run PRIVATE BK_MAIN_HANDLED)
bk_stage_shaders(03_triangle_run)

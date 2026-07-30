add_executable(05_compute main.c)
target_link_libraries(05_compute PRIVATE bielik bk_warnings)
bk_stage_shaders(05_compute)

add_executable(05_compute_run main.c)
target_link_libraries(05_compute_run PRIVATE bielik bk_warnings)
target_compile_definitions(05_compute_run PRIVATE BK_MAIN_HANDLED)
bk_stage_shaders(05_compute_run)

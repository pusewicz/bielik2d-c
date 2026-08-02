# No bk_stage_shaders calls here, deliberately -- unlike every other sample directory
# (see 07_instanced/CMakeLists.txt for the contrast). bk_draw's shader bytecode is
# compiled straight into libbielik (Task 4's bk_draw_shaders.h), so there is no
# shaders/ directory for this sample's binary to load at runtime. Adding them back
# would be a silent no-op, not a build error, so don't "restore" them if diffing
# against another sample's CMakeLists.txt.
add_executable(08_draw main.c)
target_link_libraries(08_draw PRIVATE bielik bk_warnings)

add_executable(08_draw_run main.c)
target_link_libraries(08_draw_run PRIVATE bielik bk_warnings)
target_compile_definitions(08_draw_run PRIVATE BK_MAIN_HANDLED)

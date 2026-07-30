find_program(BK_GLSLC_EXE glslc)
find_program(BK_SPIRV_CROSS_EXE spirv-cross)

# Regenerates shaders/<name>.<stage>.spv and .msl from shaders/<name>.<vert|frag> via
# glslc + spirv-cross. The regenerated files are committed to the repo, not produced
# fresh by every build -- this is a no-op (just logs) if the tools aren't installed,
# so shader authoring stays an occasional opt-in step, not a build-time requirement.
function(bk_compile_shader)
    set(one_value_args NAME STAGE)
    cmake_parse_arguments(ARG "" "${one_value_args}" "" ${ARGN})

    if(NOT BK_GLSLC_EXE OR NOT BK_SPIRV_CROSS_EXE)
        message(STATUS "glslc/spirv-cross not found -- using committed shader bytecode for ${ARG_NAME}.${ARG_STAGE}")
        return()
    endif()

    if(ARG_STAGE STREQUAL "vertex")
        set(glslc_stage "vertex")
        set(src_ext "vert")
    elseif(ARG_STAGE STREQUAL "fragment")
        set(glslc_stage "fragment")
        set(src_ext "frag")
    elseif(ARG_STAGE STREQUAL "compute")
        set(glslc_stage "compute")
        set(src_ext "comp")
    else()
        message(FATAL_ERROR "bk_compile_shader: unknown STAGE '${ARG_STAGE}' (expected vertex, fragment, or compute)")
    endif()

    set(src "${PROJECT_SOURCE_DIR}/shaders/${ARG_NAME}.${src_ext}")
    set(spv "${PROJECT_SOURCE_DIR}/shaders/${ARG_NAME}.${ARG_STAGE}.spv")
    set(msl "${PROJECT_SOURCE_DIR}/shaders/${ARG_NAME}.${ARG_STAGE}.msl")

    add_custom_command(
        OUTPUT "${spv}"
        COMMAND "${BK_GLSLC_EXE}" -fshader-stage=${glslc_stage} "${src}" -o "${spv}"
        DEPENDS "${src}"
        COMMENT "glslc: ${ARG_NAME}.${src_ext} -> ${ARG_NAME}.${ARG_STAGE}.spv"
        VERBATIM
    )
    add_custom_command(
        OUTPUT "${msl}"
        COMMAND "${BK_SPIRV_CROSS_EXE}" --msl "${spv}" --output "${msl}"
        DEPENDS "${spv}"
        COMMENT "spirv-cross: ${ARG_NAME}.${ARG_STAGE}.spv -> ${ARG_NAME}.${ARG_STAGE}.msl"
        VERBATIM
    )
    add_custom_target(bk_shader_${ARG_NAME}_${ARG_STAGE} DEPENDS "${spv}" "${msl}")
endfunction()

# Copies shaders/ next to TARGET's built binary (POST_BUILD) so it can load shader
# bytecode with a path relative to its own executable (via SDL_GetBasePath) no
# matter what working directory it's run from.
function(bk_stage_shaders TARGET)
    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${PROJECT_SOURCE_DIR}/shaders" "$<TARGET_FILE_DIR:${TARGET}>/shaders"
        COMMENT "Staging shaders/ next to ${TARGET}"
    )
endfunction()

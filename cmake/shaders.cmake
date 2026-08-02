find_program(BK_GLSLC_EXE glslc)
find_program(BK_SPIRV_CROSS_EXE spirv-cross)

# Regenerates shaders/<name>.<stage>.spv and .msl from shaders/<name>.<vert|frag> via
# glslc + spirv-cross. The regenerated files are committed to the repo, not produced
# fresh by every build -- this is a no-op (just logs) if the tools aren't installed,
# so shader authoring stays an occasional opt-in step, not a build-time requirement.
# MSL_DECORATION_BINDING: translate MSL from a second SPIR-V built with
# -DBK_MSL_BINDINGS, using spirv-cross's --msl-decoration-binding (binding number used
# directly as the [[buffer]]/[[texture]] index). Needed when a shader declares both a
# storage buffer and a uniform buffer in the same stage: SDL_GPU's SPIR-V convention
# puts storage in the lower-numbered set, but its MSL convention wants uniforms at the
# lower [[buffer]] index, and spirv-cross's default (set, binding) sort produces the
# opposite pairing. That mismatch does not fail at pipeline creation -- it drops the
# command buffer at draw time with nothing logged. See DEVIATIONS.md.
function(bk_compile_shader)
    set(options MSL_DECORATION_BINDING)
    set(one_value_args NAME STAGE)
    cmake_parse_arguments(ARG "${options}" "${one_value_args}" "" ${ARGN})

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
    if(ARG_MSL_DECORATION_BINDING)
        # The MSL-binding SPIR-V is an intermediate, not shipped bytecode, so it lives
        # in the build tree rather than beside the committed .spv/.msl files.
        set(msl_spv "${CMAKE_CURRENT_BINARY_DIR}/${ARG_NAME}.${ARG_STAGE}.mslbindings.spv")
        add_custom_command(
            OUTPUT "${msl_spv}"
            COMMAND "${BK_GLSLC_EXE}" -fshader-stage=${glslc_stage} -DBK_MSL_BINDINGS=1
                    "${src}" -o "${msl_spv}"
            DEPENDS "${src}"
            COMMENT "glslc: ${ARG_NAME}.${src_ext} -> ${ARG_NAME}.${ARG_STAGE} MSL-binding SPIR-V"
            VERBATIM
        )
        add_custom_command(
            OUTPUT "${msl}"
            COMMAND "${BK_SPIRV_CROSS_EXE}" --msl --msl-decoration-binding "${msl_spv}"
                    --output "${msl}"
            DEPENDS "${msl_spv}"
            COMMENT "spirv-cross: ${ARG_NAME}.${ARG_STAGE} -> ${ARG_NAME}.${ARG_STAGE}.msl"
            VERBATIM
        )
    else()
        add_custom_command(
            OUTPUT "${msl}"
            COMMAND "${BK_SPIRV_CROSS_EXE}" --msl "${spv}" --output "${msl}"
            DEPENDS "${spv}"
            COMMENT "spirv-cross: ${ARG_NAME}.${ARG_STAGE}.spv -> ${ARG_NAME}.${ARG_STAGE}.msl"
            VERBATIM
        )
    endif()
    add_custom_target(bk_shader_${ARG_NAME}_${ARG_STAGE} DEPENDS "${spv}" "${msl}")
endfunction()

# Mirrors shaders/ next to TARGET's built binary so it can load shader bytecode with
# a path relative to its own executable (via SDL_GetBasePath) no matter what working
# directory it's run from. This is a custom target -- always considered out of date --
# rather than a POST_BUILD command, because POST_BUILD only fires when TARGET relinks:
# regenerating bytecode alone would otherwise leave a stale copy staged.
# Files deleted from shaders/ are not pruned from the staged copy.
function(bk_stage_shaders TARGET)
    add_custom_target(bk_stage_shaders_${TARGET}
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
                "${PROJECT_SOURCE_DIR}/shaders" "$<TARGET_FILE_DIR:${TARGET}>/shaders"
        COMMENT "Staging shaders/ next to ${TARGET}"
        VERBATIM
    )
    add_dependencies(${TARGET} bk_stage_shaders_${TARGET})
endfunction()

# Compiles the committed bytecode for NAME into a C header of byte arrays, so the
# framework's own shaders need no runtime files staged beside a consumer's binary.
# Regenerates whenever the bytecode changes.
function(bk_embed_shader)
    set(one_value_args NAME)
    cmake_parse_arguments(ARG "" "${one_value_args}" "" ${ARGN})
    set(out "${CMAKE_BINARY_DIR}/generated/bk_${ARG_NAME}_shaders.h")
    set(inputs
        "${PROJECT_SOURCE_DIR}/shaders/${ARG_NAME}.vertex.spv"
        "${PROJECT_SOURCE_DIR}/shaders/${ARG_NAME}.vertex.msl"
        "${PROJECT_SOURCE_DIR}/shaders/${ARG_NAME}.fragment.spv"
        "${PROJECT_SOURCE_DIR}/shaders/${ARG_NAME}.fragment.msl")
    add_custom_command(
        OUTPUT "${out}"
        COMMAND ${CMAKE_COMMAND}
                -DBK_NAME=${ARG_NAME}
                -DBK_SHADER_DIR=${PROJECT_SOURCE_DIR}/shaders
                -DBK_OUT=${out}
                -P "${PROJECT_SOURCE_DIR}/cmake/embed_shader.cmake"
        DEPENDS ${inputs} "${PROJECT_SOURCE_DIR}/cmake/embed_shader.cmake"
        COMMENT "Embedding ${ARG_NAME} shader bytecode -> bk_${ARG_NAME}_shaders.h"
        VERBATIM)
    add_custom_target(bk_embed_${ARG_NAME} DEPENDS "${out}")
endfunction()

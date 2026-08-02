# Usage: cmake -DBK_NAME=<shader> -DBK_SHADER_DIR=<dir> -DBK_OUT=<header> -P embed_shader.cmake
#
# Turns four committed bytecode files into one C header of byte arrays. [[maybe_unused]]
# because the header is included by both src/bk_draw.c and tests/test_draw.c, and
# neither reads every array -- -Werror would reject the unused ones otherwise.
set(body "// Generated from ${BK_SHADER_DIR}/${BK_NAME}.*. Do not edit.\n#pragma once\n")
foreach(stage vertex fragment)
    foreach(format spv msl)
        set(path "${BK_SHADER_DIR}/${BK_NAME}.${stage}.${format}")
        file(READ "${path}" hex HEX)
        string(REGEX MATCHALL "([A-Fa-f0-9][A-Fa-f0-9])" bytes "${hex}")
        list(LENGTH bytes count)
        string(REPLACE ";" ", 0x" joined "${bytes}")
        set(symbol "bk__${BK_NAME}_${stage}_${format}")
        string(APPEND body
            "[[maybe_unused]] static const unsigned char ${symbol}[] = { 0x${joined} };\n"
            "[[maybe_unused]] static const unsigned long ${symbol}_size = ${count};\n")
    endforeach()
endforeach()
file(WRITE "${BK_OUT}" "${body}")

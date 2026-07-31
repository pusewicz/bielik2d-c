# `format` reformats in place; `format-check` (--dry-run -Werror, same as CI's
# `format` job in .github/workflows/ci.yml) fails if anything would change.
# Both walk the same four directories -- keep this glob and that job's `find`
# in sync if the layout changes.
if(NOT PROJECT_IS_TOP_LEVEL)
    return()
endif()

find_program(BK_CLANG_FORMAT NAMES clang-format-22 clang-format)

if(NOT BK_CLANG_FORMAT)
    message(STATUS "clang-format not found -- format/format-check targets unavailable")
    return()
endif()

file(GLOB_RECURSE BK_FORMAT_SOURCES CONFIGURE_DEPENDS
    include/*.c include/*.h
    src/*.c src/*.h
    tests/*.c tests/*.h
    samples/*.c samples/*.h
)

add_custom_target(format
    COMMAND ${BK_CLANG_FORMAT} -i ${BK_FORMAT_SOURCES}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Formatting sources with clang-format"
    VERBATIM
)

add_custom_target(format-check
    COMMAND ${BK_CLANG_FORMAT} --dry-run -Werror ${BK_FORMAT_SOURCES}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Checking source formatting with clang-format"
    VERBATIM
)

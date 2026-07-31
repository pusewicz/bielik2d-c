add_library(bk_warnings INTERFACE)
target_compile_options(bk_warnings INTERFACE
    # The Visual Studio ClangCL toolset enables a much broader warning set than plain
    # clang's defaults (things like -Wpre-c23-compat, -Wpadded, -Wreserved-macro-
    # identifier) -- -Wno-everything resets that back to a clean slate before this
    # project's own warnings are enabled below, so behavior matches plain clang/
    # AppleClang on Linux/macOS (a no-op there, since -Weverything isn't implicitly on).
    -Wno-everything
    -Wall -Wextra -Wshadow -Wstrict-prototypes -Wvla
)

option(BK_WERROR "Treat warnings as errors" OFF)
if(BK_WERROR)
    target_compile_options(bk_warnings INTERFACE -Werror)
endif()

option(BK_SANITIZE "Enable ASan/UBSan in Debug builds" ON)
if(BK_SANITIZE AND (CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_SYSTEM_NAME STREQUAL "Darwin"))
    target_compile_options(bk_warnings INTERFACE
        $<$<CONFIG:Debug>:-fsanitize=address,undefined>
        $<$<CONFIG:Debug>:-fno-sanitize-recover=all>
    )
    target_link_options(bk_warnings INTERFACE $<$<CONFIG:Debug>:-fsanitize=address,undefined>)
endif()

add_library(bk_warnings INTERFACE)
target_compile_options(bk_warnings INTERFACE
    -Wall -Wextra -Wshadow -Wstrict-prototypes -Wvla
)

option(BK_WERROR "Treat warnings as errors" OFF)
if(BK_WERROR)
    target_compile_options(bk_warnings INTERFACE -Werror)
endif()

option(BK_SANITIZE "Enable ASan/UBSan in Debug builds" ON)
if(BK_SANITIZE AND CMAKE_BUILD_TYPE STREQUAL "Debug" AND (CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_SYSTEM_NAME STREQUAL "Darwin"))
    target_compile_options(bk_warnings INTERFACE -fsanitize=address,undefined)
    target_link_options(bk_warnings INTERFACE -fsanitize=address,undefined)
endif()

# Canonical reverse-DNS application identity for bundle and desktop/portal
# declarations. Plugin manifest IDs are independent functional identifiers.
set(ECHO_APPLICATION_ID "io.github.hoowhoami.echomusicnext")

# Project warnings are private build requirements, not requirements imposed on
# consumers of a module. Targets opt in without global compiler flags.
add_library(echo_build_options INTERFACE)
option(ECHO_ENABLE_SANITIZERS "Instrument owned C/C++ with ASan and UBSan" OFF)
option(ECHO_ENABLE_COVERAGE "Instrument owned C/C++ for gcov/llvm-cov reports" OFF)
if(ECHO_ENABLE_SANITIZERS OR ECHO_ENABLE_COVERAGE)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        message(FATAL_ERROR "Sanitizer/coverage instrumentation requires Clang or GCC")
    endif()
endif()
if(ECHO_ENABLE_SANITIZERS)
    target_compile_options(
        echo_build_options INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer
    )
    target_link_options(echo_build_options INTERFACE -fsanitize=address,undefined)
endif()
if(ECHO_ENABLE_COVERAGE)
    target_compile_options(echo_build_options INTERFACE --coverage -O0 -g)
    target_link_options(echo_build_options INTERFACE --coverage)
endif()
if(MSVC)
    target_compile_options(echo_build_options INTERFACE /W4 /permissive-)
else()
    target_compile_options(echo_build_options INTERFACE -Wall -Wextra -Wpedantic)
endif()

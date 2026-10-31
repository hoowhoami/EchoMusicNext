# The workaround is build-only and can be disabled for unaffected toolchains.
# Preserve caller launchers, and leave other generators/platforms unchanged.
option(ECHO_MSVC_LINK_RESPONSE_WORKAROUND "Split long MSVC/Ninja linker response lines" ON)
if(WIN32
   AND MSVC
   AND CMAKE_GENERATOR MATCHES "Ninja"
   AND ECHO_MSVC_LINK_RESPONSE_WORKAROUND
)
    find_package(Python3 3.12 REQUIRED COMPONENTS Interpreter)
    foreach(echo_link_language IN ITEMS C CXX)
        set(echo_link_launcher "${Python3_EXECUTABLE}"
                               "${CMAKE_CURRENT_LIST_DIR}/../tools/build/msvc_link.py"
        )
        if(CMAKE_${echo_link_language}_LINKER_LAUNCHER)
            list(APPEND echo_link_launcher ${CMAKE_${echo_link_language}_LINKER_LAUNCHER})
        endif()
        set(CMAKE_${echo_link_language}_LINKER_LAUNCHER "${echo_link_launcher}")
    endforeach()
endif()

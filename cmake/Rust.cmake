find_program(CARGO_EXECUTABLE cargo REQUIRED)
set(ECHO_CARGO_COMMAND "${CARGO_EXECUTABLE}")
if(APPLE)
    set(ECHO_CARGO_COMMAND
        "${CMAKE_COMMAND}" -E env "MACOSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}"
        "${CARGO_EXECUTABLE}"
    )
endif()
set(ECHO_CARGO_DIR
    "${PROJECT_SOURCE_DIR}/target"
    CACHE PATH "Cargo-managed native artifact directory (serialize builds sharing this cache)"
)
# Match CMake's selected build configuration, including multi-config generators.
# RelWithDebInfo/MinSizeRel use Cargo's optimized release profile; Debug and an
# unspecified single-config build use dev. Imported locations are concrete paths:
# IMPORTED_LOCATION itself does not evaluate generator expressions.
set(ECHO_CARGO_OPTIMIZED "$<CONFIG:Release,RelWithDebInfo,MinSizeRel>")
set(ECHO_CARGO_PROFILE "$<IF:${ECHO_CARGO_OPTIMIZED},release,debug>")
if(WIN32)
    set(ECHO_RUST_FILENAME echo_ffi.dll)
    set(ECHO_RUST_IMPLIB "${ECHO_CARGO_DIR}/${ECHO_CARGO_PROFILE}/echo_ffi.dll.lib")
elseif(APPLE)
    set(ECHO_RUST_FILENAME libecho_ffi.dylib)
else()
    set(ECHO_RUST_FILENAME libecho_ffi.so)
endif()
set(ECHO_RUST_LIBRARY "${ECHO_CARGO_DIR}/${ECHO_CARGO_PROFILE}/${ECHO_RUST_FILENAME}")

# Always let Cargo decide freshness; CMake does not attempt to mirror Cargo's
# dependency graph. Header checking never rewrites committed source during a build.
add_custom_target(
    echo_cargo
    COMMAND ${ECHO_CARGO_COMMAND} run --locked -p ffi-header -- --check
    COMMAND ${ECHO_CARGO_COMMAND} build --locked -p echo-ffi --target-dir "${ECHO_CARGO_DIR}"
            "$<${ECHO_CARGO_OPTIMIZED}:--release>"
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    BYPRODUCTS "${ECHO_RUST_LIBRARY}" ${ECHO_RUST_IMPLIB}
    USES_TERMINAL VERBATIM COMMAND_EXPAND_LISTS
)
add_library(echo_ffi SHARED IMPORTED GLOBAL)
set_target_properties(
    echo_ffi PROPERTIES IMPORTED_LOCATION "${ECHO_CARGO_DIR}/debug/${ECHO_RUST_FILENAME}"
)
set(ECHO_RUST_CONFIGURATIONS Debug Release RelWithDebInfo MinSizeRel ${CMAKE_CONFIGURATION_TYPES}
                             ${CMAKE_BUILD_TYPE}
)
list(REMOVE_DUPLICATES ECHO_RUST_CONFIGURATIONS)
foreach(config IN LISTS ECHO_RUST_CONFIGURATIONS)
    string(TOUPPER "${config}" config_upper)
    if(config_upper MATCHES "^(RELEASE|RELWITHDEBINFO|MINSIZEREL)$")
        set(profile release)
    else()
        set(profile debug)
    endif()
    set_property(
        TARGET echo_ffi
        APPEND
        PROPERTY IMPORTED_CONFIGURATIONS "${config}"
    )
    set_target_properties(
        echo_ffi PROPERTIES "IMPORTED_LOCATION_${config_upper}"
                            "${ECHO_CARGO_DIR}/${profile}/${ECHO_RUST_FILENAME}"
    )
    if(WIN32)
        set_target_properties(
            echo_ffi PROPERTIES "IMPORTED_IMPLIB_${config_upper}"
                                "${ECHO_CARGO_DIR}/${profile}/echo_ffi.dll.lib"
        )
    endif()
endforeach()
if(WIN32)
    set_target_properties(
        echo_ffi PROPERTIES IMPORTED_IMPLIB "${ECHO_CARGO_DIR}/debug/echo_ffi.dll.lib"
    )
endif()
add_dependencies(echo_ffi echo_cargo)

# Runtime consumers opt in explicitly; the imported target selects the same DLL
# as the linker for the active configuration. No Rust ABI is exported to QML.
function(echo_copy_rust_runtime target)
    if(WIN32)
        add_custom_command(
            TARGET ${target}
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:echo_ffi>"
                    "$<TARGET_FILE_DIR:${target}>"
            VERBATIM
        )
    endif()
endfunction()

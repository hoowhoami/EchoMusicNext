find_package(Git QUIET)
set(ECHO_SOURCE_REVISION unknown)
set(ECHO_SOURCE_STATE unknown)
if(GIT_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        OUTPUT_VARIABLE ECHO_SOURCE_REVISION
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=normal
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        OUTPUT_VARIABLE ECHO_SOURCE_DIRTY
    )
    if(ECHO_SOURCE_DIRTY)
        set(ECHO_SOURCE_STATE dirty)
    else()
        set(ECHO_SOURCE_STATE clean)
    endif()
endif()
string(TIMESTAMP ECHO_BUILD_TIME "%Y%m%dT%H%M%SZ" UTC)
set(ECHO_BUILD_ID "${ECHO_BUILD_TIME}-${ECHO_SOURCE_REVISION}-${ECHO_SOURCE_STATE}")
configure_file(
    "${PROJECT_SOURCE_DIR}/cmake/EchoBuildInfo.h.in"
    "${CMAKE_BINARY_DIR}/generated/EchoBuildInfo.h" @ONLY
)
# Preserve optimized native symbols in Release as well as RelWithDebInfo.
if(MSVC)
    # Embed per-object debug data so compiler-cache hits do not depend on a
    # shared compilation PDB. Crashpad also compiles ASM_MARMASM/ASM_MASM,
    # whose assemblers reject /Z7. Keep it on C/C++ translation units only.
    # /DEBUG:FULL still emits the final release PDB.
    add_compile_options("$<$<AND:$<CONFIG:Release>,$<COMPILE_LANGUAGE:C,CXX>>:/Z7>")
    add_link_options(
        "$<$<CONFIG:Release>:/DEBUG:FULL>" "$<$<CONFIG:Release>:/OPT:REF>"
        "$<$<CONFIG:Release>:/OPT:ICF>"
    )
elseif(APPLE)
    add_compile_options("$<$<CONFIG:Release>:-g>")
else()
    add_compile_options("$<$<CONFIG:Release>:-g1>")
    add_link_options(-Wl,--build-id=sha1)
endif()

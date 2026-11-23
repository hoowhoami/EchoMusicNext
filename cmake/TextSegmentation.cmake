# ICU word breaking is optional for the host and negotiated by plugins. There
# is deliberately no fallback to Qt's differently tailored word boundaries.
option(ECHO_TEXT_SEGMENTATION "Enable ICU-backed plugin word segmentation when available" ON)
add_library(echo_text_segmentation INTERFACE)
if(ECHO_TEXT_SEGMENTATION)
    if(ICU_ROOT)
        # Qt may also have cached its own discovery earlier in this configure.
        # An explicit application prefix must be searched independently.
        foreach(
            _echo_icu_entry
            INCLUDE_DIR
            UC_LIBRARY_RELEASE
            UC_LIBRARY_DEBUG
            I18N_LIBRARY_RELEASE
            I18N_LIBRARY_DEBUG
            DATA_LIBRARY_RELEASE
            DATA_LIBRARY_DEBUG
        )
            unset(ICU_${_echo_icu_entry} CACHE)
            unset(ICU_${_echo_icu_entry})
        endforeach()
    endif()
    find_package(ICU 78.2 QUIET COMPONENTS uc i18n data)
endif()
if(ECHO_TEXT_SEGMENTATION
   AND ICU_FOUND
   AND ICU_VERSION VERSION_LESS 79
)
    # Qt's SDK may have already created ICU:: targets for its own ICU ABI.
    # FindICU updates discovery variables but never replaces existing targets;
    # keep our selected headers/libraries together without changing Qt's targets.
    foreach(_echo_icu_component i18n uc data)
        string(TOUPPER "${_echo_icu_component}" _echo_icu_upper)
        set(_echo_icu_library "ICU_${_echo_icu_upper}_LIBRARY")
        set(_echo_icu_target "EchoTextICU::${_echo_icu_component}")
        add_library(${_echo_icu_target} UNKNOWN IMPORTED)
        set_target_properties(
            ${_echo_icu_target} PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${ICU_INCLUDE_DIRS}"
        )
        if(EXISTS "${${_echo_icu_library}}")
            set_target_properties(
                ${_echo_icu_target} PROPERTIES IMPORTED_LOCATION "${${_echo_icu_library}}"
            )
        endif()
        foreach(_echo_icu_config RELEASE DEBUG)
            if(EXISTS "${${_echo_icu_library}_${_echo_icu_config}}")
                set_property(
                    TARGET ${_echo_icu_target}
                    APPEND
                    PROPERTY IMPORTED_CONFIGURATIONS ${_echo_icu_config}
                )
                set_target_properties(
                    ${_echo_icu_target} PROPERTIES IMPORTED_LOCATION_${_echo_icu_config}
                                                   "${${_echo_icu_library}_${_echo_icu_config}}"
                )
            endif()
        endforeach()
    endforeach()
    if(CMAKE_DL_LIBS)
        set_target_properties(
            EchoTextICU::uc PROPERTIES INTERFACE_LINK_LIBRARIES "${CMAKE_DL_LIBS}"
        )
    endif()
    target_compile_definitions(echo_text_segmentation INTERFACE ECHO_HAS_ICU_SEGMENTATION=1)
    target_link_libraries(
        echo_text_segmentation INTERFACE EchoTextICU::i18n EchoTextICU::uc EchoTextICU::data
    )
    # Packaging consumes the libraries actually selected by CMake, rather than
    # guessing from a second ICU_ROOT or the deployment machine's system ICU.
    file(REAL_PATH "${ICU_INCLUDE_DIR}/.." _echo_icu_prefix)
    file(
        GENERATE
        OUTPUT "${CMAKE_BINARY_DIR}/generated/echo-icu-$<CONFIG>.json"
        CONTENT
            "{\n  \"version\": \"${ICU_VERSION}\",\n  \"root\": \"${_echo_icu_prefix}\",\n  \"uc\": \"$<TARGET_FILE:EchoTextICU::uc>\",\n  \"i18n\": \"$<TARGET_FILE:EchoTextICU::i18n>\",\n  \"data\": \"$<TARGET_FILE:EchoTextICU::data>\"\n}\n"
    )
    message(STATUS "Plugin word segmentation: ICU ${ICU_VERSION}")
else()
    file(REMOVE "${CMAKE_BINARY_DIR}/generated/echo-icu-Release.json")
    if(ECHO_DISTRIBUTION_BUILD)
        message(
            FATAL_ERROR
                "Distribution builds require ICU word segmentation. Run scripts/install-icu.py and configure with -DICU_ROOT=<prefix>."
        )
    endif()
    message(STATUS "Plugin word segmentation unavailable: compatible ICU 78.x was not found")
endif()

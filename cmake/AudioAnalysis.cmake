# Optional separately licensed process. The host library never links Folia or wgpu.
set(ECHO_AUDIO_ANALYSIS_DEPENDENCIES
    "${PROJECT_SOURCE_DIR}/build/audio-analysis/dependencies"
    CACHE PATH "Verified audio-analysis model/native dependency cache"
)
set(ECHO_AUDIO_ANALYSIS_ENABLED_DEFAULT OFF)
if(APPLE
   AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$"
   AND EXISTS "${ECHO_AUDIO_ANALYSIS_DEPENDENCIES}/wgpu-native-v29.0.1.1/lib/libwgpu_native.dylib"
)
    set(ECHO_AUDIO_ANALYSIS_ENABLED_DEFAULT ON)
endif()
option(ECHO_ENABLE_AUDIO_ANALYSIS_HELPER
       "Build the independently licensed native GPU analysis process"
       ${ECHO_AUDIO_ANALYSIS_ENABLED_DEFAULT}
)
if(ECHO_ENABLE_AUDIO_ANALYSIS_HELPER)
    set(WGPU_NATIVE_ROOT
        "${ECHO_AUDIO_ANALYSIS_DEPENDENCIES}/wgpu-native-v29.0.1.1"
        CACHE PATH "Pinned native WebGPU release"
    )
    add_subdirectory(
        "${PROJECT_SOURCE_DIR}/tools/audio-analysis-helper"
        "${PROJECT_BINARY_DIR}/audio-analysis-helper"
    )
    add_dependencies(echo_plugin_sdk echo-audio-analysis-helper)
    target_compile_definitions(
        echo_plugin_sdk
        PRIVATE
            ECHO_AUDIO_ANALYSIS_BUILD_ROOT="${PROJECT_BINARY_DIR}"
            ECHO_AUDIO_ANALYSIS_HELPER_PATH="$<TARGET_FILE:echo-audio-analysis-helper>"
            ECHO_AUDIO_ANALYSIS_MODEL_PATH="${ECHO_AUDIO_ANALYSIS_DEPENDENCIES}"
            ECHO_AUDIO_ANALYSIS_RUNTIME_PATH="${PROJECT_SOURCE_DIR}/tools/audio-analysis-helper/generated"
    )
endif()

function(echo_deploy_audio_analysis target)
    if(NOT TARGET echo-audio-analysis-helper)
        return()
    endif()
    add_dependencies(${target} echo-audio-analysis-helper)
    if(APPLE)
        set(_data "$<TARGET_BUNDLE_DIR:${target}>/Contents/Resources/audio-analysis")
        set(_helper "$<TARGET_BUNDLE_DIR:${target}>/Contents/Helpers")
        set(_library "$<TARGET_BUNDLE_DIR:${target}>/Contents/Frameworks")
        set(_install_data "echomusicnext.app/Contents/Resources/audio-analysis")
        set(_install_helper "echomusicnext.app/Contents/Helpers")
        set(_install_library "echomusicnext.app/Contents/Frameworks")
    else()
        set(_data "$<TARGET_FILE_DIR:${target}>/../share/echomusicnext/audio-analysis")
        set(_helper "$<TARGET_FILE_DIR:${target}>")
        set(_library "$<TARGET_FILE_DIR:${target}>/../lib")
        set(_install_data "share/echomusicnext/audio-analysis")
        set(_install_helper "bin")
        set(_install_library "lib")
    endif()
    set(_model_commands)
    foreach(_model beat_this-wgsl.bin htdemucs-wgsl.bin)
        if(EXISTS "${ECHO_AUDIO_ANALYSIS_DEPENDENCIES}/${_model}")
            list(
                APPEND
                _model_commands
                COMMAND
                ${CMAKE_COMMAND}
                -E
                copy_if_different
                "${ECHO_AUDIO_ANALYSIS_DEPENDENCIES}/${_model}"
                "${_data}/models"
            )
        endif()
    endforeach()
    add_custom_command(
        TARGET ${target}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_helper}" "${_library}" "${_data}/runtime"
                "${_data}/models" "${_data}/licenses"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:echo-audio-analysis-helper>"
                "${_helper}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${WGPU_NATIVE_LIBRARY}" "${_library}"
        COMMAND
            ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/tools/audio-analysis-helper/generated/graph-runtime.js"
            "${_data}/runtime"
        COMMAND
            ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/tools/audio-analysis-helper/generated/signal-runtime.js"
            "${_data}/runtime"
        COMMAND
            ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/tools/audio-analysis-helper/generated/provenance.json"
            "${_data}/runtime" ${_model_commands}
        COMMAND
            ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/tools/audio-analysis-helper/LICENSE"
            "${_data}/licenses/Folia-AGPL-3.0.txt"
        COMMAND
            ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/tools/audio-analysis-helper/THIRD_PARTY_NOTICES.md"
            "${_data}/licenses"
        COMMAND
            ${CMAKE_COMMAND} -E copy_directory
            "${PROJECT_SOURCE_DIR}/tools/audio-analysis-helper/third_party"
            "${_data}/licenses/third_party"
    )
    install(TARGETS echo-audio-analysis-helper RUNTIME DESTINATION "${_install_helper}")
    install(FILES "${WGPU_NATIVE_LIBRARY}" DESTINATION "${_install_library}")
    install(
        FILES "${PROJECT_SOURCE_DIR}/tools/audio-analysis-helper/generated/graph-runtime.js"
              "${PROJECT_SOURCE_DIR}/tools/audio-analysis-helper/generated/signal-runtime.js"
              "${PROJECT_SOURCE_DIR}/tools/audio-analysis-helper/generated/provenance.json"
        DESTINATION "${_install_data}/runtime"
    )
    install(
        FILES "${ECHO_AUDIO_ANALYSIS_DEPENDENCIES}/beat_this-wgsl.bin"
              "${ECHO_AUDIO_ANALYSIS_DEPENDENCIES}/htdemucs-wgsl.bin"
        DESTINATION "${_install_data}/models"
        OPTIONAL
    )
    install(FILES "${PROJECT_SOURCE_DIR}/tools/audio-analysis-helper/LICENSE"
                  "${PROJECT_SOURCE_DIR}/tools/audio-analysis-helper/THIRD_PARTY_NOTICES.md"
            DESTINATION "${_install_data}/licenses"
    )
    install(DIRECTORY "${PROJECT_SOURCE_DIR}/tools/audio-analysis-helper/third_party/"
            DESTINATION "${_install_data}/licenses/third_party"
    )
    # Preferred form of the separately licensed component, including generated provenance.
    install(
        DIRECTORY "${PROJECT_SOURCE_DIR}/tools/audio-analysis-helper/"
        DESTINATION "${_install_data}/source"
        PATTERN "tests" EXCLUDE
    )
endfunction()

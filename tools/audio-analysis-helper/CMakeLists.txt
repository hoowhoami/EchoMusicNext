cmake_minimum_required(VERSION 3.24)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(EchoAudioAnalysisTool LANGUAGES CXX)
    set(_analysis_tests_default ON)
else()
    set(_analysis_tests_default OFF)
endif()
option(ECHO_AUDIO_ANALYSIS_TESTS "Build analysis tests with the host source tree"
       ${_analysis_tests_default}
)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_AUTOMOC ON)
enable_testing()
find_package(Qt6 6.12 REQUIRED COMPONENTS Core Qml)
if(ECHO_AUDIO_ANALYSIS_TESTS)
    find_package(Qt6 6.12 REQUIRED COMPONENTS Test Concurrent)
endif()
set(WGPU_NATIVE_ROOT
    ""
    CACHE PATH "Pinned wgpu-native shared release"
)
find_library(
    WGPU_NATIVE_LIBRARY wgpu_native
    HINTS "${WGPU_NATIVE_ROOT}/lib" REQUIRED
    NO_DEFAULT_PATH
)
set(WGPU_NATIVE_LIBRARY
    "${WGPU_NATIVE_LIBRARY}"
    CACHE FILEPATH "Pinned native WebGPU shared library" FORCE
)
add_library(
    analysis-native STATIC NativeGraph.cpp SourceGraph.cpp SignalProfile.cpp ModelSnapshot.cpp
)
target_include_directories(
    analysis-native
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${WGPU_NATIVE_ROOT}/include"
)
target_link_libraries(
    analysis-native
    PUBLIC Qt6::Core Qt6::Qml
    PRIVATE "${WGPU_NATIVE_LIBRARY}"
)
add_executable(echo-audio-analysis-helper worker.cpp)
target_link_libraries(echo-audio-analysis-helper PRIVATE analysis-native)
set_target_properties(echo-audio-analysis-helper PROPERTIES MACOSX_BUNDLE FALSE)
if(APPLE)
    set_target_properties(
        echo-audio-analysis-helper PROPERTIES INSTALL_RPATH "@executable_path/../Frameworks"
    )
elseif(UNIX)
    set_target_properties(echo-audio-analysis-helper PROPERTIES INSTALL_RPATH "$ORIGIN/../lib")
endif()
if(ECHO_AUDIO_ANALYSIS_TESTS)
    add_executable(analysis-native-test tests/NativeGraphTest.cpp)
    target_link_libraries(analysis-native-test PRIVATE analysis-native Qt6::Test)
    target_compile_definitions(
        analysis-native-test
        PRIVATE ANALYSIS_RUNTIME_SCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/generated/graph-runtime.js"
                ANALYSIS_TYPED_ARRAY_SCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/typed-array-compat.mjs"
                ANALYSIS_MODEL_DIRECTORY="${CMAKE_BINARY_DIR}/../dependencies"
    )
    add_test(NAME analysis-native-test COMMAND analysis-native-test)
    add_library(analysis-broker STATIC ../../services/audioanalysis/AnalysisBroker.cpp)
    target_include_directories(analysis-broker PUBLIC ../../services/audioanalysis)
    target_link_libraries(
        analysis-broker
        PUBLIC Qt6::Core
        PRIVATE Qt6::Concurrent
    )
    add_executable(analysis-broker-test tests/BrokerTest.cpp)
    target_link_libraries(analysis-broker-test PRIVATE analysis-broker Qt6::Test)
    target_compile_definitions(
        analysis-broker-test
        PRIVATE ANALYSIS_HELPER="$<TARGET_FILE:echo-audio-analysis-helper>"
                ANALYSIS_MODEL_DIRECTORY="${CMAKE_BINARY_DIR}/../dependencies"
                ANALYSIS_RUNTIME_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/generated"
    )
    add_test(NAME analysis-broker-test COMMAND analysis-broker-test)
    target_sources(analysis-broker PRIVATE ../../services/audioanalysis/AnalysisService.cpp)
    add_executable(analysis-service-test tests/ServiceTest.cpp)
    target_link_libraries(analysis-service-test PRIVATE analysis-broker Qt6::Test)
    add_test(NAME analysis-service-test COMMAND analysis-service-test)

    add_executable(analysis-resume-test tests/ResumeIntegrationTest.cpp)
    target_link_libraries(analysis-resume-test PRIVATE analysis-broker Qt6::Test)
    target_compile_definitions(
        analysis-resume-test
        PRIVATE ANALYSIS_HELPER="$<TARGET_FILE:echo-audio-analysis-helper>"
                ANALYSIS_MODEL_DIRECTORY="${CMAKE_BINARY_DIR}/../dependencies"
                ANALYSIS_RUNTIME_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/generated"
    )
    add_test(NAME analysis-resume-test COMMAND analysis-resume-test)

endif()

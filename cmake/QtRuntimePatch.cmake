# The 6.12.0 generator GC fix is part of the SDK, not an application workaround.
get_filename_component(echo_qt_prefix "${Qt6_DIR}/../../.." REALPATH)
set(echo_qv4_receipt "${echo_qt_prefix}/.echo-qv4-gc.json")
if(NOT EXISTS "${echo_qv4_receipt}")
    message(
        FATAL_ERROR
            "Qt 6.12.0 needs the QV4 generator GC patch. Run python scripts/patch-qt-qml.py --qt-prefix \"${echo_qt_prefix}\""
    )
endif()
file(READ "${echo_qv4_receipt}" echo_qv4_metadata)
string(
    JSON
    echo_qv4_version
    ERROR_VARIABLE
    echo_qv4_error
    GET
    "${echo_qv4_metadata}"
    version
)
if(echo_qv4_error OR NOT echo_qv4_version STREQUAL "6.12.0")
    message(FATAL_ERROR "Invalid QtQml patch receipt: ${echo_qv4_receipt}")
endif()
string(
    JSON
    echo_qv4_expected_patch
    ERROR_VARIABLE
    echo_qv4_error
    GET
    "${echo_qv4_metadata}"
    patch_sha256
)
file(SHA256 "${PROJECT_SOURCE_DIR}/tools/qt/patches/qtdeclarative-6.12.0-qv4-generator-gc.patch"
     echo_qv4_actual_patch
)
if(echo_qv4_error OR NOT echo_qv4_expected_patch STREQUAL echo_qv4_actual_patch)
    message(FATAL_ERROR "QtQml SDK patch differs from the required source patch; repair the SDK")
endif()
get_target_property(echo_qv4_library Qt6::Qml IMPORTED_LOCATION_RELEASE)
string(
    JSON
    echo_qv4_expected_binary
    ERROR_VARIABLE
    echo_qv4_error
    GET
    "${echo_qv4_metadata}"
    binary_sha256
)
if(echo_qv4_error OR NOT EXISTS "${echo_qv4_library}")
    message(FATAL_ERROR "Cannot verify the patched QtQml runtime")
endif()
file(SHA256 "${echo_qv4_library}" echo_qv4_actual_binary)
if(NOT echo_qv4_expected_binary STREQUAL echo_qv4_actual_binary)
    message(FATAL_ERROR "QtQml binary no longer matches its patch receipt; repair the SDK")
endif()

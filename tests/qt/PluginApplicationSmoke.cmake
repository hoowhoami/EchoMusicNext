# Check both the process result and actual fixture lifecycle: an empty catalog
# or an early application shutdown must not count as a plugin integration pass.
execute_process(
    COMMAND "${APP}" --smoke-test --plugin-dir "${PLUGIN}" --log-dir "${LOG_DIR}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE errors
    TIMEOUT 20
)
set(transcript "${output}\n${errors}")
message("${transcript}")
if(NOT result STREQUAL "0")
    message(FATAL_ERROR "Plugin application smoke exited with ${result}")
endif()
foreach(marker "Plugin application fixture activated" "Plugin application fixture deactivated")
    string(FIND "${transcript}" "${marker}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Plugin application smoke did not execute: ${marker}")
    endif()
endforeach()

# Verify the production composition executes dependency stages in order and
# completes asynchronous shutdown after real plugin deactivation.
set(previous -1)
foreach(marker
        "Bootstrap phase: createShell" "Bootstrap phase: createNetworkPlayback"
        "Bootstrap phase: createAccount" "Bootstrap phase: createPages"
        "Bootstrap phase: createPlugins" "Bootstrap phase: registerRoutes" "Bootstrap phase: wireShutdown"
        "Bootstrap phase: wireSmoke" "Bootstrap phase: mountUi"
        "Application shutdown started" "Plugin application fixture deactivated"
        "Application shutdown complete")
    string(FIND "${transcript}" "${marker}" position)
    if(position LESS_EQUAL previous)
        message(FATAL_ERROR "Composition lifecycle marker missing/out of order: ${marker}")
    endif()
    set(previous ${position})
endforeach()

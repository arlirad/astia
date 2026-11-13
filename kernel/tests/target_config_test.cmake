set(test_root "${CMAKE_CURRENT_BINARY_DIR}/target-config")
file(REMOVE_RECURSE "${test_root}")

execute_process(
        COMMAND "${CMAKE_COMMAND}" -S "${ASTIA_KERNEL_SOURCE_DIR}"
        -B "${test_root}/x86_64"
        -DARCH=x86_64 -DPLATFORM=virt -DBOOT=virt
        -DASTIA_BUILD_TESTS=OFF
        RESULT_VARIABLE x86_result
        OUTPUT_VARIABLE x86_output
        ERROR_VARIABLE x86_error)
if (x86_result)
    message(FATAL_ERROR "x86_64 target configuration failed:\n${x86_output}\n${x86_error}")
endif ()

execute_process(
        COMMAND "${CMAKE_COMMAND}" -S "${ASTIA_KERNEL_SOURCE_DIR}"
        -B "${test_root}/aarch64"
        -DARCH=aarch64 -DPLATFORM=virt -DBOOT=virt
        -DASTIA_BUILD_TESTS=OFF
        RESULT_VARIABLE aarch64_result
        OUTPUT_VARIABLE aarch64_output
        ERROR_VARIABLE aarch64_error)
if (aarch64_result EQUAL 0)
    message(FATAL_ERROR "AArch64 target configuration unexpectedly succeeded")
endif ()
if (NOT "${aarch64_output}\n${aarch64_error}" MATCHES "Unsupported AArch64 kernel target")
    message(FATAL_ERROR
            "AArch64 target configuration failed without the supported diagnostic:\n"
            "${aarch64_output}\n${aarch64_error}")
endif ()

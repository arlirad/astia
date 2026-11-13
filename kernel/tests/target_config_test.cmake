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

function(assert_core_fiber_compiled build_root expected)
    file(GLOB_RECURSE dependency_files
            "${build_root}/src/core/CMakeFiles/core.dir/DependInfo.cmake")
    if (NOT dependency_files)
        message(FATAL_ERROR "core dependency metadata was not generated in ${build_root}")
    endif ()
    list(GET dependency_files 0 dependency_file)
    file(READ "${dependency_file}" dependency_text)
    string(FIND "${dependency_text}" "fiber.cpp" fiber_position)
    if (expected AND fiber_position EQUAL -1)
        message(FATAL_ERROR "experimental fiber was not compiled")
    endif ()
    if (NOT expected AND NOT fiber_position EQUAL -1)
        message(FATAL_ERROR "production core unexpectedly compiles fiber.cpp")
    endif ()
endfunction()

assert_core_fiber_compiled("${test_root}/x86_64" FALSE)

execute_process(
        COMMAND "${CMAKE_COMMAND}" -S "${ASTIA_KERNEL_SOURCE_DIR}"
        -B "${test_root}/experimental-fiber"
        -DARCH=x86_64 -DPLATFORM=virt -DBOOT=virt
        -DASTIA_BUILD_TESTS=OFF
        -DASTIA_BUILD_EXPERIMENTAL_FIBER=ON
        RESULT_VARIABLE fiber_result
        OUTPUT_VARIABLE fiber_output
        ERROR_VARIABLE fiber_error)
if (fiber_result)
    message(FATAL_ERROR "experimental fiber configuration failed:\n${fiber_output}\n${fiber_error}")
endif ()
assert_core_fiber_compiled("${test_root}/experimental-fiber" TRUE)

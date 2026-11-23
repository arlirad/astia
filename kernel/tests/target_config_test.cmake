set(test_root "${CMAKE_CURRENT_BINARY_DIR}/target-config")
file(REMOVE_RECURSE "${test_root}")

if (EXISTS "${ASTIA_KERNEL_SOURCE_DIR}/cmake/Linker.cmake")
    message(FATAL_ERROR "obsolete cmake/Linker.cmake is still present")
endif ()

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

file(READ "${test_root}/x86_64/CMakeCache.txt" x86_cache)
set(expected_linker "${ASTIA_KERNEL_SOURCE_DIR}/cmake/linker/virt-x86_64.ld")
string(FIND "${x86_cache}" "ASTIA_LINKER_SCRIPT:INTERNAL=${expected_linker}" linker_position)
if (linker_position EQUAL -1)
    message(FATAL_ERROR
            "x86_64 configuration did not select the target-derived linker script:\n"
            "${x86_cache}")
endif ()

execute_process(
        COMMAND "${CMAKE_COMMAND}" --build "${test_root}/x86_64"
        --target astiakrn --verbose
        RESULT_VARIABLE x86_build_result
        OUTPUT_VARIABLE x86_build_output
        ERROR_VARIABLE x86_build_error)
if (x86_build_result)
    message(FATAL_ERROR "x86_64 kernel build failed:\n${x86_build_output}\n${x86_build_error}")
endif ()
if (NOT "${x86_build_output}\n${x86_build_error}" MATCHES "-T[ \t]+.*virt-x86_64\\.ld")
    message(FATAL_ERROR
            "x86_64 kernel build did not use the target-derived linker script:\n"
            "${x86_build_output}\n${x86_build_error}")
endif ()

execute_process(
        COMMAND "${CMAKE_COMMAND}" -S "${ASTIA_KERNEL_SOURCE_DIR}"
        -B "${test_root}/aarch64"
        -DARCH=aarch64 -DPLATFORM=virt -DBOOT=virt
        -DASTIA_EXPERIMENTAL_AARCH64=ON
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

execute_process(
        COMMAND "${CMAKE_COMMAND}" -S "${ASTIA_KERNEL_SOURCE_DIR}"
        -B "${test_root}/x86_64-experimental-aarch64"
        -DARCH=x86_64 -DPLATFORM=virt -DBOOT=virt
        -DASTIA_EXPERIMENTAL_AARCH64=ON
        -DASTIA_BUILD_TESTS=OFF
        RESULT_VARIABLE experimental_result
        OUTPUT_VARIABLE experimental_output
        ERROR_VARIABLE experimental_error)
if (experimental_result)
    message(FATAL_ERROR
            "experimental AArch64 source configuration failed:\n"
            "${experimental_output}\n${experimental_error}")
endif ()

execute_process(
        COMMAND "${CMAKE_COMMAND}" --build
                "${test_root}/x86_64-experimental-aarch64"
                --target astiakrn --verbose
        RESULT_VARIABLE experimental_kernel_build_result
        OUTPUT_VARIABLE experimental_kernel_build_output
        ERROR_VARIABLE experimental_kernel_build_error)
if (experimental_kernel_build_result)
    message(FATAL_ERROR
            "x86_64 production build with experimental AArch64 source enabled failed:\n"
            "${experimental_kernel_build_output}\n${experimental_kernel_build_error}")
endif ()

file(GLOB_RECURSE experimental_kernel_dependencies
        "${test_root}/x86_64-experimental-aarch64/*/CMakeFiles/astiakrn.dir/DependInfo.cmake")
if (NOT experimental_kernel_dependencies)
    message(FATAL_ERROR "production kernel dependency metadata was not generated")
endif ()
foreach (dependency_file IN LISTS experimental_kernel_dependencies)
    file(READ "${dependency_file}" dependency_text)
    string(FIND "${dependency_text}" "/arch/aarch64/" aarch64_position)
    if (NOT aarch64_position EQUAL -1)
        message(FATAL_ERROR
                "production kernel target depends on experimental AArch64 source:\n"
                "${dependency_file}\n${dependency_text}")
    endif ()
endforeach ()

execute_process(
        COMMAND "${CMAKE_COMMAND}" --build
                "${test_root}/x86_64-experimental-aarch64"
                --target astia_experimental_aarch64 --verbose
        RESULT_VARIABLE experimental_port_build_result
        OUTPUT_VARIABLE experimental_port_build_output
        ERROR_VARIABLE experimental_port_build_error)
if (experimental_port_build_result)
    message(FATAL_ERROR
            "standalone experimental AArch64 port build failed:\n"
            "${experimental_port_build_output}\n${experimental_port_build_error}")
endif ()

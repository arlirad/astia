# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Arlirad

foreach(_required
        ASTIA_SOURCE_DIR
        ASTIA_PARENT_BINARY_DIR
        ASTIA_CLEAN_TEST_BINARY_DIR
        ASTIA_GENERATOR)
    if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "Missing required variable: ${_required}")
    endif()
endforeach()

get_filename_component(_source_dir "${ASTIA_SOURCE_DIR}" REALPATH)
get_filename_component(_parent_binary_dir "${ASTIA_PARENT_BINARY_DIR}" REALPATH)
get_filename_component(_clean_binary_dir "${ASTIA_CLEAN_TEST_BINARY_DIR}" ABSOLUTE)
get_filename_component(_clean_parent "${_clean_binary_dir}" DIRECTORY)
if(NOT _clean_parent STREQUAL _parent_binary_dir OR
   _clean_binary_dir STREQUAL _source_dir OR
   _clean_binary_dir STREQUAL _parent_binary_dir)
    message(FATAL_ERROR
        "Clean dev_initrd test build must be a dedicated child of the parent binary directory")
endif()

set(_unsafe_binary_dir "${ASTIA_PARENT_BINARY_DIR}/dev-initrd-unsafe-run-test")
file(REMOVE_RECURSE "${_unsafe_binary_dir}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${_source_dir}" -B "${_unsafe_binary_dir}"
            -G "${ASTIA_GENERATOR}"
            "-DASTIA_DEV_INITRD_RUN=${_unsafe_binary_dir}/kernel"
    RESULT_VARIABLE unsafe_configure_result
    OUTPUT_VARIABLE unsafe_configure_output
    ERROR_VARIABLE unsafe_configure_error)
if(unsafe_configure_result EQUAL 0 OR
   NOT "${unsafe_configure_output}${unsafe_configure_error}" MATCHES
       "overlaps a protected build/source directory")
    message(FATAL_ERROR "Unsafe ASTIA_DEV_INITRD_RUN path was not rejected")
endif()
file(REMOVE_RECURSE "${_unsafe_binary_dir}")

file(REMOVE_RECURSE "${_clean_binary_dir}")
file(MAKE_DIRECTORY "${_clean_binary_dir}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${_source_dir}" -B "${_clean_binary_dir}"
            -G "${ASTIA_GENERATOR}"
            -DASTIA_BUILD_TESTS=ON
            -DASTIA_CROSS_USERSPACE=ON
            "-DASTIA_DEV_INITRD_RUN=${_clean_binary_dir}/dev initrd"
            "-DASTIA_DEV_INITRD_ZSH_BUILD=${_clean_binary_dir}/zsh build"
            "-DASTIA_DEV_INITRD_NANO_BUILD=${_clean_binary_dir}/nano build"
    RESULT_VARIABLE configure_result
    OUTPUT_FILE "${_clean_binary_dir}/configure.log"
    ERROR_FILE "${_clean_binary_dir}/configure-error.log")
if(NOT configure_result EQUAL 0)
    message(FATAL_ERROR
        "Clean dev_initrd CMake configure failed; see ${_clean_binary_dir}/configure*.log")
endif()

set(_archive "${_clean_binary_dir}/dev initrd/initrd.cpio")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env QEMU=astia-qemu-not-installed
            "${CMAKE_COMMAND}" --build "${_clean_binary_dir}"
            --target dev_initrd --parallel 8
    RESULT_VARIABLE build_result
    OUTPUT_FILE "${_clean_binary_dir}/build.log"
    ERROR_FILE "${_clean_binary_dir}/build-error.log")
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR
        "Clean dev_initrd build failed; see ${_clean_binary_dir}/build*.log")
endif()

set(_nano_config_log "${_clean_binary_dir}/nano build/config.log")
if(NOT EXISTS "${_nano_config_log}")
    message(FATAL_ERROR "Clean dev_initrd build did not create ${_nano_config_log}")
endif()
file(READ "${_nano_config_log}" _nano_config)
if(NOT _nano_config MATCHES "libclang_rt\\.builtins\\.a" OR
   _nano_config MATCHES "libclang_rt_builtins\\.a")
    message(FATAL_ERROR
        "Nano configure linker configuration does not use the generated builtins alias")
endif()

foreach(_artifact
        "${_clean_binary_dir}/kernel/src/astiakrn_runtime"
        "${_clean_binary_dir}/kernel/tests/fixtures/boot_initrd.cpio"
        "${_archive}")
    if(NOT EXISTS "${_artifact}")
        message(FATAL_ERROR "Clean dev_initrd build did not create ${_artifact}")
    endif()
endforeach()

execute_process(
    COMMAND cpio -it --quiet --no-absolute-filenames
    INPUT_FILE "${_archive}"
    RESULT_VARIABLE manifest_result
    OUTPUT_VARIABLE manifest)
if(NOT manifest_result EQUAL 0)
    message(FATAL_ERROR "Clean dev_initrd output is not a readable cpio archive")
endif()
foreach(_entry IN ITEMS "sbin/init" "bin/zsh" "bin/nano" "bin/poweroff" "bin/reboot"
        "etc/zshenv")
    if(NOT manifest MATCHES "(^|\n)${_entry}(\n|$)")
        message(FATAL_ERROR "Clean dev_initrd archive is missing ${_entry}")
    endif()
endforeach()

find_program(_python3 python3 REQUIRED)
execute_process(
    COMMAND "${_python3}" -c
            "import os, sys; print(os.stat(sys.argv[1]).st_mtime_ns)"
            "${_archive}"
    RESULT_VARIABLE timestamp_result
    OUTPUT_VARIABLE timestamp_before
    OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT timestamp_result EQUAL 0)
    message(FATAL_ERROR "Unable to inspect clean dev_initrd archive timestamp")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env QEMU=astia-qemu-not-installed
            "${CMAKE_COMMAND}" --build "${_clean_binary_dir}"
            --target dev_initrd --parallel 8
    RESULT_VARIABLE repeat_result
    OUTPUT_FILE "${_clean_binary_dir}/repeat-build.log"
    ERROR_FILE "${_clean_binary_dir}/repeat-build-error.log")
if(NOT repeat_result EQUAL 0)
    message(FATAL_ERROR
        "Repeated dev_initrd build failed; see ${_clean_binary_dir}/repeat-build*.log")
endif()
execute_process(
    COMMAND "${_python3}" -c
            "import os, sys; print(os.stat(sys.argv[1]).st_mtime_ns)"
            "${_archive}"
    RESULT_VARIABLE repeat_timestamp_result
    OUTPUT_VARIABLE timestamp_after
    OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT repeat_timestamp_result EQUAL 0 OR
   NOT timestamp_before STREQUAL timestamp_after)
    message(FATAL_ERROR "Repeated dev_initrd build was not a no-op")
endif()

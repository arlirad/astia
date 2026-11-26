# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Arlirad

if(ASTIA_BUILD_TESTS)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    enable_language(C)
    add_library(pkg_getopt OBJECT ${CMAKE_CURRENT_SOURCE_DIR}/../libc/src/stdlib/getopt.c)
    target_include_directories(pkg_getopt PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/../libc/include
            ${CMAKE_CURRENT_SOURCE_DIR}/../kernel/include)
    astia_apply_host_sanitizers(pkg_getopt)
    add_executable(pkg_cli_parser cli_parser.c $<TARGET_OBJECTS:pkg_getopt>)
    astia_apply_host_sanitizers(pkg_cli_parser)
    add_test(NAME pkg_host_tests
            COMMAND ${CMAKE_COMMAND} -E env
                    PKG_GETOPT_HELPER=$<TARGET_FILE:pkg_cli_parser>
                    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pkg.py
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..)
    set_tests_properties(pkg_host_tests PROPERTIES
            LABELS "pkg;host;deterministic"
            TIMEOUT 60)
    astia_add_test_label(pkg_host_tests "sanitizer-host")
endif()

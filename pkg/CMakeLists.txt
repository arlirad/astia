# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Arlirad

if(ASTIA_BUILD_TESTS)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME pkg_host_tests
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pkg.py
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..)
    set_tests_properties(pkg_host_tests PROPERTIES
            LABELS "pkg;host;deterministic"
            TIMEOUT 60)
endif()

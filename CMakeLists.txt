cmake_minimum_required(VERSION 3.20)

# The repository-level project is the canonical entry point for IDEs.  The
# kernel remains independently configurable from kernel/, while libc keeps
# its separate freestanding toolchain and install/sysroot workflow.
project(astia LANGUAGES NONE)

enable_testing()

option(ASTIA_BUILD_TESTS "Build and register host-side unit tests" OFF)

add_subdirectory(kernel)
add_subdirectory(modules EXCLUDE_FROM_ALL)

if(ASTIA_BUILD_TESTS)
    foreach(_astia_module_test_target
            astia_module_abi_layout_target
            astia_module_c_compatibility
            virtio_blk_unit_target)
        if(NOT TARGET ${_astia_module_test_target})
            message(FATAL_ERROR
                    "ASTIA_BUILD_TESTS requires module target ${_astia_module_test_target}")
        endif()
    endforeach()

    add_custom_target(astia_module_host_tests ALL)
    add_dependencies(astia_module_host_tests
            astia_module_abi_layout_target
            astia_module_c_compatibility
            virtio_blk_unit_target)

    add_test(NAME astia_module_host_targets
            COMMAND ${CMAKE_COMMAND}
            -DASTIA_MODULE_ABI_LAYOUT=${CMAKE_CURRENT_BINARY_DIR}/modules/tests/module_abi_layout
            -DASTIA_VIRTIO_BLK_UNIT=${CMAKE_CURRENT_BINARY_DIR}/modules/virtio-blk/virtio_blk_unit
            -P ${CMAKE_CURRENT_SOURCE_DIR}/modules/tests/host_tests_regression.cmake)
endif()

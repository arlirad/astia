# Read-only ext4 filesystem module.

set(ASTIA_EXT4_SOURCES
        module_main.cpp
        ext4_mount.cpp
        ext4_file.cpp
        ext4_reader.cpp
        ext4_disk.cpp
        ext4_space.cpp
        ext4_extent.cpp
        ext4_dir.cpp)

set(ASTIA_EXT4_LOCAL_HEADERS
        ${CMAKE_CURRENT_SOURCE_DIR}/ext4.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ext4_reader.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ext4_disk.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ext4_space.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ext4_extent.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ext4_dir.hpp)

set(ASTIA_EXT4_ABI_HEADERS
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/api/aio.h
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/api/errno.h
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/common.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/core/cancellation.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/core/endian.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/core/spinlock.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/core/task.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/error.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/fs.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/fs/contracts.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/fs/file.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/fs/filesystem.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/fs/module.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/fs/mount.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/fs/namespace.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/fs/open_file.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/fs/search.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/fs/types.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/log.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/mm/heap.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/mm/phys_region.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/mm/share.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/mm/shptr.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/optional.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/span.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/strings.hpp)

set(ASTIA_EXT4_OBJECT ${CMAKE_CURRENT_BINARY_DIR}/ext4.km)
set(ASTIA_EXT4_OBJECTS)

foreach(_astia_ext4_source IN LISTS ASTIA_EXT4_SOURCES)
    get_filename_component(_astia_ext4_name ${_astia_ext4_source} NAME_WE)
    set(_astia_ext4_object ${CMAKE_CURRENT_BINARY_DIR}/${_astia_ext4_name}.module.o)
    add_custom_command(
            OUTPUT ${_astia_ext4_object}
            COMMAND ${CMAKE_CXX_COMPILER} -std=c++20 -O2 -c
                    -ffreestanding -fno-builtin -fno-pic -fno-pie
                    -fno-exceptions -fno-rtti -fno-threadsafe-statics
                    -fno-stack-protector -fno-omit-frame-pointer -mno-red-zone
                    -mcmodel=large -fno-asynchronous-unwind-tables
                    -fno-use-cxa-atexit -mgeneral-regs-only -DASTIA_MODULE_ET_REL=1
                    -Wall -Wextra -Werror -pedantic
                    -MMD -MF ${_astia_ext4_object}.d -MT ${_astia_ext4_object}
                    -I${ASTIA_KERNEL_SOURCE_DIR}/include
                    -I${ASTIA_ARCH_INCLUDE_DIR}
                    -I${CMAKE_CURRENT_SOURCE_DIR}
                    -c ${CMAKE_CURRENT_SOURCE_DIR}/${_astia_ext4_source}
                    -o ${_astia_ext4_object}
            DEPFILE ${_astia_ext4_object}.d
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${_astia_ext4_source}
                    ${ASTIA_EXT4_ABI_HEADERS}
                    ${ASTIA_EXT4_LOCAL_HEADERS}
            COMMENT "Compiling ext4 module ${_astia_ext4_source}"
            VERBATIM)
    list(APPEND ASTIA_EXT4_OBJECTS ${_astia_ext4_object})
endforeach()

add_custom_command(
        OUTPUT ${ASTIA_EXT4_OBJECT}
        COMMAND ${CMAKE_LINKER} -r ${ASTIA_EXT4_OBJECTS} -o ${ASTIA_EXT4_OBJECT}
        DEPENDS ${ASTIA_EXT4_OBJECTS}
        COMMENT "Linking ext4 loadable module"
        VERBATIM)

add_custom_target(ext4_module ALL DEPENDS ${ASTIA_EXT4_OBJECT})

set(ASTIA_EXT4_UNIT ${CMAKE_CURRENT_BINARY_DIR}/ext4_unit)

add_custom_command(
        OUTPUT ${ASTIA_EXT4_UNIT}
        COMMAND ${CMAKE_CXX_COMPILER} -std=c++20 -DASTIA_HOST_TEST=1
                -Wall -Wextra -Werror -pedantic
                ${ASTIA_HOST_SANITIZER_COMPILE_OPTIONS}
                -I${ASTIA_KERNEL_SOURCE_DIR}/include
                -I${ASTIA_ARCH_INCLUDE_DIR}
                -I${ASTIA_KERNEL_SOURCE_DIR}/tests/support
                -I${CMAKE_CURRENT_SOURCE_DIR}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/ext4_test.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/ext4_reader.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/ext4_disk.cpp
                ${ASTIA_KERNEL_SOURCE_DIR}/tests/support/main.cpp
                ${ASTIA_HOST_SANITIZER_LINK_OPTIONS}
                -o ${ASTIA_EXT4_UNIT}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/ext4_test.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/ext4_reader.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/ext4_disk.cpp
                ${ASTIA_KERNEL_SOURCE_DIR}/tests/support/main.cpp
                ${ASTIA_KERNEL_SOURCE_DIR}/tests/support/test.hpp
                ${ASTIA_EXT4_LOCAL_HEADERS}
                ${ASTIA_EXT4_ABI_HEADERS}
        VERBATIM)

add_custom_target(ext4_unit_target ALL DEPENDS ${ASTIA_EXT4_UNIT})

set(ASTIA_EXT4_DISK_UNIT ${CMAKE_CURRENT_BINARY_DIR}/ext4_disk_unit)

add_custom_command(
        OUTPUT ${ASTIA_EXT4_DISK_UNIT}
        COMMAND ${CMAKE_CXX_COMPILER} -std=c++20 -DASTIA_HOST_TEST=1
                -Wall -Wextra -Werror -pedantic
                ${ASTIA_HOST_SANITIZER_COMPILE_OPTIONS}
                -I${ASTIA_KERNEL_SOURCE_DIR}/include
                -I${ASTIA_ARCH_INCLUDE_DIR}
                -I${CMAKE_CURRENT_SOURCE_DIR}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/ext4_disk_test.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/ext4_disk.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/ext4_reader.cpp
                ${ASTIA_HOST_SANITIZER_LINK_OPTIONS}
                -o ${ASTIA_EXT4_DISK_UNIT}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/ext4_disk_test.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/ext4_disk.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/ext4_reader.cpp
                ${ASTIA_EXT4_LOCAL_HEADERS}
                ${ASTIA_EXT4_ABI_HEADERS}
        VERBATIM)

add_custom_target(ext4_disk_unit_target ALL DEPENDS ${ASTIA_EXT4_DISK_UNIT})

set(ASTIA_EXT4_EXTENT_UNIT ${CMAKE_CURRENT_BINARY_DIR}/ext4_extent_unit)

add_custom_command(
        OUTPUT ${ASTIA_EXT4_EXTENT_UNIT}
        COMMAND ${CMAKE_CXX_COMPILER} -std=c++20 -DASTIA_HOST_TEST=1
                -Wall -Wextra -Werror -pedantic
                ${ASTIA_HOST_SANITIZER_COMPILE_OPTIONS}
                -I${ASTIA_KERNEL_SOURCE_DIR}/include
                -I${ASTIA_ARCH_INCLUDE_DIR}
                -I${CMAKE_CURRENT_SOURCE_DIR}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/ext4_extent_test.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/ext4_disk.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/ext4_reader.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/ext4_space.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/ext4_extent.cpp
                ${ASTIA_HOST_SANITIZER_LINK_OPTIONS}
                -o ${ASTIA_EXT4_EXTENT_UNIT}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/ext4_extent_test.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/ext4_disk.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/ext4_reader.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/ext4_space.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/ext4_extent.cpp
                ${ASTIA_EXT4_LOCAL_HEADERS}
                ${ASTIA_EXT4_ABI_HEADERS}
        VERBATIM)

add_custom_target(ext4_extent_unit_target ALL DEPENDS ${ASTIA_EXT4_EXTENT_UNIT})

set(ASTIA_EXT4_DIR_UNIT ${CMAKE_CURRENT_BINARY_DIR}/ext4_dir_unit)

add_custom_command(
        OUTPUT ${ASTIA_EXT4_DIR_UNIT}
        COMMAND ${CMAKE_CXX_COMPILER} -std=c++20 -DASTIA_HOST_TEST=1
                -Wall -Wextra -Werror -pedantic
                ${ASTIA_HOST_SANITIZER_COMPILE_OPTIONS}
                -I${ASTIA_KERNEL_SOURCE_DIR}/include
                -I${ASTIA_ARCH_INCLUDE_DIR}
                -I${CMAKE_CURRENT_SOURCE_DIR}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/ext4_dir_test.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/ext4_disk.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/ext4_reader.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/ext4_space.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/ext4_extent.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/ext4_dir.cpp
                ${ASTIA_HOST_SANITIZER_LINK_OPTIONS}
                -o ${ASTIA_EXT4_DIR_UNIT}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/ext4_dir_test.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/ext4_disk.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/ext4_reader.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/ext4_space.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/ext4_extent.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/ext4_dir.cpp
                ${ASTIA_EXT4_LOCAL_HEADERS}
                ${ASTIA_EXT4_ABI_HEADERS}
        VERBATIM)

add_custom_target(ext4_dir_unit_target ALL DEPENDS ${ASTIA_EXT4_DIR_UNIT})

if(ASTIA_BUILD_TESTS)
    add_test(NAME ext4_unit COMMAND ${ASTIA_EXT4_UNIT})
    astia_add_test_label(ext4_unit "sanitizer-host")

    find_program(ASTIA_EXT4_MKFS_EXT4 mkfs.ext4)
    find_program(ASTIA_EXT4_E2FSCK e2fsck)
    if(ASTIA_EXT4_MKFS_EXT4 AND ASTIA_EXT4_E2FSCK)
        set(_astia_ext4_disk_work ${CMAKE_CURRENT_BINARY_DIR}/ext4-disk-test)
        add_custom_target(ext4_disk_test_target
                COMMAND ${CMAKE_COMMAND} -E make_directory ${_astia_ext4_disk_work}
                COMMAND ${CMAKE_COMMAND} -E env
                        EXT4_DISK_TOOL=${ASTIA_EXT4_DISK_UNIT}
                        ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_disk_test.sh
                        ${_astia_ext4_disk_work}/fixture.img
                        ${_astia_ext4_disk_work}
                DEPENDS ${ASTIA_EXT4_DISK_UNIT}
                VERBATIM)
        add_test(NAME ext4_disk_test
                COMMAND ${CMAKE_COMMAND} -E env
                        EXT4_DISK_TOOL=${ASTIA_EXT4_DISK_UNIT}
                        ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_disk_test.sh
                        ${_astia_ext4_disk_work}/fixture.img
                        ${_astia_ext4_disk_work})
        astia_add_test_label(ext4_disk_test "ext4;filesystem;sanitizer-host")

        set(_astia_ext4_extent_work ${CMAKE_CURRENT_BINARY_DIR}/ext4-extent-test)
        add_custom_target(ext4_extent_test_target
                COMMAND ${CMAKE_COMMAND} -E make_directory ${_astia_ext4_extent_work}
                COMMAND ${CMAKE_COMMAND} -E env
                        EXT4_EXTENT_TOOL=${ASTIA_EXT4_EXTENT_UNIT}
                        ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_extent_test.sh
                        ${_astia_ext4_extent_work}/fixture.img
                        ${_astia_ext4_extent_work}
                DEPENDS ${ASTIA_EXT4_EXTENT_UNIT}
                VERBATIM)
        add_test(NAME ext4_extent_test
                COMMAND ${CMAKE_COMMAND} -E env
                        EXT4_EXTENT_TOOL=${ASTIA_EXT4_EXTENT_UNIT}
                        ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_extent_test.sh
                        ${_astia_ext4_extent_work}/fixture.img
                        ${_astia_ext4_extent_work})
        astia_add_test_label(ext4_extent_test "ext4;filesystem;sanitizer-host")

        set(_astia_ext4_dir_work ${CMAKE_CURRENT_BINARY_DIR}/ext4-dir-test)
        add_custom_target(ext4_dir_test_target
                COMMAND ${CMAKE_COMMAND} -E make_directory ${_astia_ext4_dir_work}
                COMMAND ${CMAKE_COMMAND} -E env
                        EXT4_DIR_TOOL=${ASTIA_EXT4_DIR_UNIT}
                        ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_dir_test.sh
                        ${_astia_ext4_dir_work}/fixture.img
                        ${_astia_ext4_dir_work}
                DEPENDS ${ASTIA_EXT4_DIR_UNIT}
                VERBATIM)
        add_test(NAME ext4_dir_test
                COMMAND ${CMAKE_COMMAND} -E env
                        EXT4_DIR_TOOL=${ASTIA_EXT4_DIR_UNIT}
                        ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_dir_test.sh
                        ${_astia_ext4_dir_work}/fixture.img
                        ${_astia_ext4_dir_work})
        astia_add_test_label(ext4_dir_test "ext4;filesystem;sanitizer-host")
    endif()
endif()

add_custom_target(ext4_qemu_probe_smoke
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run-qemu-load.sh
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        VERBATIM)

if(ASTIA_BUILD_TESTS)
    add_test(NAME ext4_qemu_module_load
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run-qemu-load.sh)
    set_tests_properties(ext4_qemu_module_load
                         PROPERTIES LABELS "module;ext4;qemu;deterministic"
                         ENVIRONMENT "ASTIA_TEST_BUILD=${CMAKE_BINARY_DIR}"
                         FIXTURES_REQUIRED astia-qemu-artifacts
                         RESOURCE_LOCK astia-qemu)
endif()

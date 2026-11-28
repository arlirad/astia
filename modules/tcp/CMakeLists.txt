# TCP module scaffold.

set(ASTIA_TCP_SOURCES
        tcp.cpp
        proto/tcp.cpp)

set(ASTIA_TCP_LOCAL_HEADERS
        ${CMAKE_CURRENT_SOURCE_DIR}/proto/tcp.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/headers/tcp.hpp)

set(ASTIA_TCP_ABI_HEADERS
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/common.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/core/endian.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/core/task.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/error.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/log.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/module.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/net.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/net/checksum.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/net/header.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/net/link.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/net/port.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/net/protocol.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/net/types.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/span.hpp)

set(ASTIA_TCP_OBJECT ${CMAKE_CURRENT_BINARY_DIR}/tcp.km)
set(ASTIA_TCP_OBJECTS)

foreach(_astia_tcp_source IN LISTS ASTIA_TCP_SOURCES)
    string(REPLACE "/" "_" _astia_tcp_name "${_astia_tcp_source}")
    string(REPLACE ".cpp" "" _astia_tcp_name "${_astia_tcp_name}")
    set(_astia_tcp_object ${CMAKE_CURRENT_BINARY_DIR}/${_astia_tcp_name}.module.o)
    add_custom_command(
            OUTPUT ${_astia_tcp_object}
            COMMAND ${CMAKE_CXX_COMPILER} -std=c++20 -O2 -c
                    -ffreestanding -fno-builtin -fno-pic -fno-pie
                    -fno-exceptions -fno-rtti -fno-threadsafe-statics
                    -fno-stack-protector -fno-omit-frame-pointer -mno-red-zone
                    -mcmodel=large -fno-asynchronous-unwind-tables
                    -fno-use-cxa-atexit -DASTIA_MODULE_ET_REL=1
                    -Wall -Wextra -Werror -pedantic
                    -MMD -MF ${_astia_tcp_object}.d -MT ${_astia_tcp_object}
                    -I${ASTIA_KERNEL_SOURCE_DIR}/include
                    -I${ASTIA_ARCH_INCLUDE_DIR}
                    -I${CMAKE_CURRENT_SOURCE_DIR}
                    -c ${CMAKE_CURRENT_SOURCE_DIR}/${_astia_tcp_source}
                    -o ${_astia_tcp_object}
            DEPFILE ${_astia_tcp_object}.d
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${_astia_tcp_source}
                    ${ASTIA_TCP_ABI_HEADERS}
                    ${ASTIA_TCP_LOCAL_HEADERS}
            COMMENT "Compiling TCP module ${_astia_tcp_source}"
            VERBATIM)
    list(APPEND ASTIA_TCP_OBJECTS ${_astia_tcp_object})
endforeach()

add_custom_command(
        OUTPUT ${ASTIA_TCP_OBJECT}
        COMMAND ${CMAKE_CXX_COMPILER} -nostdlib -r ${ASTIA_TCP_OBJECTS}
                -o ${ASTIA_TCP_OBJECT}
        DEPENDS ${ASTIA_TCP_OBJECTS}
        COMMENT "Linking TCP loadable module"
        VERBATIM)

add_custom_target(tcp_module ALL DEPENDS ${ASTIA_TCP_OBJECT})


set(ASTIA_TCP_UNIT ${CMAKE_CURRENT_BINARY_DIR}/tcp_unit)

add_custom_command(
        OUTPUT ${ASTIA_TCP_UNIT}
        COMMAND ${CMAKE_CXX_COMPILER} -std=c++20 -DASTIA_HOST_TEST=1
                -Wall -Wextra -Werror -pedantic
                ${ASTIA_HOST_SANITIZER_COMPILE_OPTIONS}
                -I${ASTIA_KERNEL_SOURCE_DIR}/include
                -I${ASTIA_ARCH_INCLUDE_DIR}
                -I${CMAKE_CURRENT_SOURCE_DIR}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/tcp_test.cpp
                ${ASTIA_HOST_SANITIZER_LINK_OPTIONS}
                -o ${ASTIA_TCP_UNIT}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/tcp_test.cpp
                ${ASTIA_TCP_LOCAL_HEADERS}
                ${ASTIA_TCP_ABI_HEADERS}
        VERBATIM)

add_custom_target(tcp_unit_target ALL DEPENDS ${ASTIA_TCP_UNIT})

if(ASTIA_BUILD_TESTS)
    add_test(NAME tcp_unit COMMAND ${ASTIA_TCP_UNIT})
    astia_add_test_label(tcp_unit "sanitizer-host")
endif()

if(ASTIA_BUILD_TESTS)
    add_test(NAME tcp_qemu_module_load
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run-qemu-load.sh)
    set_tests_properties(tcp_qemu_module_load
                         PROPERTIES LABELS "module;tcp;qemu;deterministic"
                         ENVIRONMENT "ASTIA_TEST_BUILD=${CMAKE_BINARY_DIR}"
                         FIXTURES_REQUIRED astia-qemu-artifacts
                         RESOURCE_LOCK astia-qemu)
endif()

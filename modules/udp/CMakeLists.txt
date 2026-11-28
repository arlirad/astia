# UDP module scaffold.

set(ASTIA_UDP_SOURCES
        udp.cpp
        proto/udp.cpp)

set(ASTIA_UDP_LOCAL_HEADERS
        ${CMAKE_CURRENT_SOURCE_DIR}/proto/udp.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/headers/udp.hpp)

set(ASTIA_UDP_ABI_HEADERS
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

set(ASTIA_UDP_OBJECT ${CMAKE_CURRENT_BINARY_DIR}/udp.km)
set(ASTIA_UDP_OBJECTS)

foreach(_astia_udp_source IN LISTS ASTIA_UDP_SOURCES)
    string(REPLACE "/" "_" _astia_udp_name "${_astia_udp_source}")
    string(REPLACE ".cpp" "" _astia_udp_name "${_astia_udp_name}")
    set(_astia_udp_object ${CMAKE_CURRENT_BINARY_DIR}/${_astia_udp_name}.module.o)
    add_custom_command(
            OUTPUT ${_astia_udp_object}
            COMMAND ${CMAKE_CXX_COMPILER} -std=c++20 -O2 -c
                    -ffreestanding -fno-builtin -fno-pic -fno-pie
                    -fno-exceptions -fno-rtti -fno-threadsafe-statics
                    -fno-stack-protector -fno-omit-frame-pointer -mno-red-zone
                    -mcmodel=large -fno-asynchronous-unwind-tables
                    -fno-use-cxa-atexit -DASTIA_MODULE_ET_REL=1
                    -Wall -Wextra -Werror -pedantic
                    -MMD -MF ${_astia_udp_object}.d -MT ${_astia_udp_object}
                    -I${ASTIA_KERNEL_SOURCE_DIR}/include
                    -I${ASTIA_ARCH_INCLUDE_DIR}
                    -I${CMAKE_CURRENT_SOURCE_DIR}
                    -c ${CMAKE_CURRENT_SOURCE_DIR}/${_astia_udp_source}
                    -o ${_astia_udp_object}
            DEPFILE ${_astia_udp_object}.d
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${_astia_udp_source}
                    ${ASTIA_UDP_ABI_HEADERS}
                    ${ASTIA_UDP_LOCAL_HEADERS}
            COMMENT "Compiling UDP module ${_astia_udp_source}"
            VERBATIM)
    list(APPEND ASTIA_UDP_OBJECTS ${_astia_udp_object})
endforeach()

add_custom_command(
        OUTPUT ${ASTIA_UDP_OBJECT}
        COMMAND ${CMAKE_CXX_COMPILER} -nostdlib -r ${ASTIA_UDP_OBJECTS}
                -o ${ASTIA_UDP_OBJECT}
        DEPENDS ${ASTIA_UDP_OBJECTS}
        COMMENT "Linking UDP loadable module"
        VERBATIM)

add_custom_target(udp_module ALL DEPENDS ${ASTIA_UDP_OBJECT})

if(ASTIA_BUILD_TESTS)
    add_test(NAME udp_qemu_module_load
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run-qemu-load.sh)
    set_tests_properties(udp_qemu_module_load
                         PROPERTIES LABELS "module;udp;qemu;deterministic"
                         ENVIRONMENT "ASTIA_TEST_BUILD=${CMAKE_BINARY_DIR}"
                         FIXTURES_REQUIRED astia-qemu-artifacts
                         RESOURCE_LOCK astia-qemu)
endif()

# IP stack module.

set(ASTIA_IP_SOURCES
        ip.cpp
        proto/ipv4.cpp
        proto/icmp.cpp
        discoverers/arp.cpp)

set(ASTIA_IP_LOCAL_HEADERS
        ${CMAKE_CURRENT_SOURCE_DIR}/config.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/discoverers/arp.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/proto/icmp.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/proto/ipv4.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/proto/mapping.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/headers/arp.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/headers/ipv4.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/headers/icmp.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/types/address.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/types/arp.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/types/icmp.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/types/protocol.hpp)

set(ASTIA_IP_ABI_HEADERS
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/atomic.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/core/completion.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/core/hook.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/core/endian.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/core/task.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/error.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/log.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/memory.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/module.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/net.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/net/address.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/rcu/vector.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/net/events.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/net/builder.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/net/bus.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/net/checksum.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/net/header.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/net/link.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/net/protocol.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/net/types.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/span.hpp)

set(ASTIA_IP_OBJECT ${CMAKE_CURRENT_BINARY_DIR}/ip.km)
set(ASTIA_IP_OBJECTS)

foreach(_astia_ip_source IN LISTS ASTIA_IP_SOURCES)
    get_filename_component(_astia_ip_name ${_astia_ip_source} NAME_WE)
    set(_astia_ip_object ${CMAKE_CURRENT_BINARY_DIR}/${_astia_ip_name}.module.o)
    add_custom_command(
            OUTPUT ${_astia_ip_object}
            COMMAND ${CMAKE_CXX_COMPILER} -std=c++20 -O2 -c
                    -ffreestanding -fno-builtin -fno-pic -fno-pie
                    -fno-exceptions -fno-rtti -fno-threadsafe-statics
                    -fno-stack-protector -fno-omit-frame-pointer -mno-red-zone
                    -mcmodel=large -fno-asynchronous-unwind-tables
                    -fno-use-cxa-atexit -DASTIA_MODULE_ET_REL=1
                    -Wall -Wextra -Werror -pedantic
                    -MMD -MF ${_astia_ip_object}.d -MT ${_astia_ip_object}
                    -I${ASTIA_KERNEL_SOURCE_DIR}/include
                    -I${ASTIA_ARCH_INCLUDE_DIR}
                    -I${CMAKE_CURRENT_SOURCE_DIR}
                    -c ${CMAKE_CURRENT_SOURCE_DIR}/${_astia_ip_source}
                    -o ${_astia_ip_object}
            DEPFILE ${_astia_ip_object}.d
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${_astia_ip_source}
                    ${ASTIA_IP_ABI_HEADERS}
                    ${ASTIA_IP_LOCAL_HEADERS}
            COMMENT "Compiling IP module ${_astia_ip_source}"
            VERBATIM)
    list(APPEND ASTIA_IP_OBJECTS ${_astia_ip_object})
endforeach()

add_custom_command(
        OUTPUT ${ASTIA_IP_OBJECT}
        COMMAND ${CMAKE_CXX_COMPILER} -nostdlib -r ${ASTIA_IP_OBJECTS}
                -o ${ASTIA_IP_OBJECT}
        DEPENDS ${ASTIA_IP_OBJECTS}
        COMMENT "Linking IP loadable module"
        VERBATIM)

add_custom_target(ip_module ALL DEPENDS ${ASTIA_IP_OBJECT})

set(ASTIA_IP_UNIT ${CMAKE_CURRENT_BINARY_DIR}/ip_unit)

add_custom_command(
        OUTPUT ${ASTIA_IP_UNIT}
        COMMAND ${CMAKE_CXX_COMPILER} -std=c++20 -DASTIA_HOST_TEST=1
                -Wall -Wextra -Werror -pedantic
                ${ASTIA_HOST_SANITIZER_COMPILE_OPTIONS}
                -I${ASTIA_KERNEL_SOURCE_DIR}/include
                -I${ASTIA_ARCH_INCLUDE_DIR}
                -I${ASTIA_KERNEL_SOURCE_DIR}/tests
                -I${CMAKE_CURRENT_SOURCE_DIR}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/ip_test.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/proto/ipv4.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/proto/icmp.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/discoverers/arp.cpp
                ${ASTIA_KERNEL_SOURCE_DIR}/src/net/protocol.cpp
                ${ASTIA_KERNEL_SOURCE_DIR}/src/net/neighbour.cpp
                ${ASTIA_KERNEL_SOURCE_DIR}/src/core/rcu_runtime.cpp
                ${ASTIA_KERNEL_SOURCE_DIR}/tests/support/cpu_relax_host.cpp
                ${ASTIA_KERNEL_SOURCE_DIR}/tests/support/interrupts_host.cpp
                ${ASTIA_KERNEL_SOURCE_DIR}/tests/support/timer_host.cpp
                ${ASTIA_KERNEL_SOURCE_DIR}/tests/support/freestanding_new.cpp
                ${ASTIA_HOST_SANITIZER_LINK_OPTIONS}
                -o ${ASTIA_IP_UNIT}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/ip_test.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/proto/ipv4.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/proto/icmp.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/discoverers/arp.cpp
                ${ASTIA_KERNEL_SOURCE_DIR}/src/net/protocol.cpp
                ${ASTIA_KERNEL_SOURCE_DIR}/src/net/neighbour.cpp
                ${ASTIA_KERNEL_SOURCE_DIR}/src/core/rcu_runtime.cpp
                ${ASTIA_KERNEL_SOURCE_DIR}/tests/support/cpu_relax_host.cpp
                ${ASTIA_KERNEL_SOURCE_DIR}/tests/support/interrupts_host.cpp
                ${ASTIA_KERNEL_SOURCE_DIR}/tests/support/timer_host.cpp
                ${ASTIA_KERNEL_SOURCE_DIR}/tests/support/timer_host.hpp
                ${ASTIA_KERNEL_SOURCE_DIR}/src/net/net_lock.hpp
                ${ASTIA_IP_LOCAL_HEADERS}
                ${ASTIA_IP_ABI_HEADERS}
        VERBATIM)

add_custom_target(ip_unit_target ALL DEPENDS ${ASTIA_IP_UNIT})

if(ASTIA_BUILD_TESTS)
    add_test(NAME ip_unit COMMAND ${ASTIA_IP_UNIT})
    astia_add_test_label(ip_unit "sanitizer-host")
endif()

add_custom_target(ip_qemu_probe_smoke
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run-qemu-load.sh
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        VERBATIM)

if(ASTIA_BUILD_TESTS)
    add_test(NAME ip_qemu_module_load
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run-qemu-load.sh)
    set_tests_properties(ip_qemu_module_load
                         PROPERTIES LABELS "module;ip;qemu;deterministic"
                         ENVIRONMENT "ASTIA_TEST_BUILD=${CMAKE_BINARY_DIR}"
                         FIXTURES_REQUIRED astia-qemu-artifacts
                         RESOURCE_LOCK astia-qemu)
endif()

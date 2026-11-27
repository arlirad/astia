# Ethernet layer module.

set(ASTIA_ETH_SOURCES
        eth.cpp
        link/link.cpp
        link/driver.cpp)

set(ASTIA_ETH_LOCAL_HEADERS
        ${CMAKE_CURRENT_SOURCE_DIR}/link/link.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/link/driver.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/headers/ethernet.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/headers/arp.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../ip/types/address.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/types/arp.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/types/ethertype.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/types/mac.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/mapping.hpp)

set(ASTIA_ETH_ABI_HEADERS
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/atomic.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/common.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/core/endian.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/core/task.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/error.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/log.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/mm/shptr.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/mm/shweak.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/module.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/net.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/net/address.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/net/header.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/net/link.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/net/types.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/net/link_bus.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/net/link_driver.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/net/protocol.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/span.hpp)

set(ASTIA_ETH_OBJECT ${CMAKE_CURRENT_BINARY_DIR}/eth.o)
set(ASTIA_ETH_OBJECTS)

foreach(_astia_eth_source IN LISTS ASTIA_ETH_SOURCES)
    get_filename_component(_astia_eth_name ${_astia_eth_source} NAME_WE)
    set(_astia_eth_object ${CMAKE_CURRENT_BINARY_DIR}/${_astia_eth_name}.module.o)
    add_custom_command(
            OUTPUT ${_astia_eth_object}
            COMMAND ${CMAKE_CXX_COMPILER} -std=c++20 -O2 -c
                    -ffreestanding -fno-builtin -fno-pic -fno-pie
                    -fno-exceptions -fno-rtti -fno-threadsafe-statics
                    -fno-stack-protector -fno-omit-frame-pointer -mno-red-zone
                    -mcmodel=large -fno-asynchronous-unwind-tables
                    -fno-use-cxa-atexit -DASTIA_MODULE_ET_REL=1
                    -Wall -Wextra -Werror -pedantic
                    -MMD -MF ${_astia_eth_object}.d -MT ${_astia_eth_object}
                    -I${ASTIA_KERNEL_SOURCE_DIR}/include
                    -I${ASTIA_ARCH_INCLUDE_DIR}
                    -I${CMAKE_CURRENT_SOURCE_DIR}
                    -I${CMAKE_CURRENT_SOURCE_DIR}/link
                    -c ${CMAKE_CURRENT_SOURCE_DIR}/${_astia_eth_source}
                    -o ${_astia_eth_object}
            DEPFILE ${_astia_eth_object}.d
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${_astia_eth_source}
                    ${ASTIA_ETH_ABI_HEADERS}
                    ${ASTIA_ETH_LOCAL_HEADERS}
            COMMENT "Compiling Ethernet module ${_astia_eth_source}"
            VERBATIM)
    list(APPEND ASTIA_ETH_OBJECTS ${_astia_eth_object})
endforeach()

add_custom_command(
        OUTPUT ${ASTIA_ETH_OBJECT}
        COMMAND ${CMAKE_CXX_COMPILER} -nostdlib -r ${ASTIA_ETH_OBJECTS}
                -o ${ASTIA_ETH_OBJECT}
        DEPENDS ${ASTIA_ETH_OBJECTS}
        COMMENT "Linking Ethernet loadable module"
        VERBATIM)

add_custom_target(eth_module ALL DEPENDS ${ASTIA_ETH_OBJECT})

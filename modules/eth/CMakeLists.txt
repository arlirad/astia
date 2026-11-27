# Ethernet layer module scaffold.

set(ASTIA_ETH_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/eth.cpp)
set(ASTIA_ETH_PART ${CMAKE_CURRENT_BINARY_DIR}/eth.module.o)
set(ASTIA_ETH_OBJECT ${CMAKE_CURRENT_BINARY_DIR}/eth.o)

add_custom_command(
        OUTPUT ${ASTIA_ETH_PART}
        COMMAND ${CMAKE_CXX_COMPILER} -std=c++20 -O2 -c
                -ffreestanding -fno-builtin -fno-pic -fno-pie
                -fno-exceptions -fno-rtti -fno-threadsafe-statics
                -fno-stack-protector -fno-omit-frame-pointer -mno-red-zone
                -mcmodel=large -fno-asynchronous-unwind-tables
                -fno-use-cxa-atexit -DASTIA_MODULE_ET_REL=1
                -Wall -Wextra -Werror -pedantic
                -MMD -MF ${ASTIA_ETH_PART}.d -MT ${ASTIA_ETH_PART}
                -I${ASTIA_KERNEL_SOURCE_DIR}/include
                -I${ASTIA_ARCH_INCLUDE_DIR}
                -c ${ASTIA_ETH_SOURCE}
                -o ${ASTIA_ETH_PART}
        DEPFILE ${ASTIA_ETH_PART}.d
        DEPENDS ${ASTIA_ETH_SOURCE}
                ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/core/task.hpp
                ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/log.hpp
                ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/module.hpp
        COMMENT "Compiling Ethernet module eth.cpp"
        VERBATIM)

add_custom_command(
        OUTPUT ${ASTIA_ETH_OBJECT}
        COMMAND ${CMAKE_CXX_COMPILER} -nostdlib -r ${ASTIA_ETH_PART}
                -o ${ASTIA_ETH_OBJECT}
        DEPENDS ${ASTIA_ETH_PART}
        COMMENT "Linking Ethernet loadable module"
        VERBATIM)

add_custom_target(eth_module ALL DEPENDS ${ASTIA_ETH_OBJECT})

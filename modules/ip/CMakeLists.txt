# IP stack module scaffold.

set(ASTIA_IP_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/ip.cpp)
set(ASTIA_IP_PART ${CMAKE_CURRENT_BINARY_DIR}/ip.module.o)
set(ASTIA_IP_OBJECT ${CMAKE_CURRENT_BINARY_DIR}/ip.o)

add_custom_command(
        OUTPUT ${ASTIA_IP_PART}
        COMMAND ${CMAKE_CXX_COMPILER} -std=c++20 -O2 -c
                -ffreestanding -fno-builtin -fno-pic -fno-pie
                -fno-exceptions -fno-rtti -fno-threadsafe-statics
                -fno-stack-protector -fno-omit-frame-pointer -mno-red-zone
                -mcmodel=large -fno-asynchronous-unwind-tables
                -fno-use-cxa-atexit -Wall -Wextra -Werror -pedantic
                -MMD -MF ${ASTIA_IP_PART}.d -MT ${ASTIA_IP_PART}
                -I${ASTIA_KERNEL_SOURCE_DIR}/include
                -I${ASTIA_ARCH_INCLUDE_DIR}
                -c ${ASTIA_IP_SOURCE}
                -o ${ASTIA_IP_PART}
        DEPFILE ${ASTIA_IP_PART}.d
        DEPENDS ${ASTIA_IP_SOURCE}
                ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/log.hpp
                ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/module.hpp
        COMMENT "Compiling IP module ip.cpp"
        VERBATIM)

add_custom_command(
        OUTPUT ${ASTIA_IP_OBJECT}
        COMMAND ${CMAKE_CXX_COMPILER} -nostdlib -r ${ASTIA_IP_PART}
                -o ${ASTIA_IP_OBJECT}
        DEPENDS ${ASTIA_IP_PART}
        COMMENT "Linking IP loadable module"
        VERBATIM)

add_custom_target(ip_module ALL DEPENDS ${ASTIA_IP_OBJECT})

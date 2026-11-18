set(ASTIA_RTL8139_OBJECT ${CMAKE_CURRENT_BINARY_DIR}/rtl8139.o)
set(ASTIA_RTL8139_UNIT ${CMAKE_CURRENT_BINARY_DIR}/rtl8139_unit)

set(ASTIA_RTL8139_ABI_HEADERS
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/module_abi_core.h
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/module_abi_pci.h
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/module_abi_memory.h
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/module_abi_io.h
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/module_abi_interrupt.h
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/module_abi.h)

add_custom_command(
        OUTPUT ${ASTIA_RTL8139_OBJECT}
        COMMAND ${CMAKE_CXX_COMPILER}
                -std=c++20 -O2 -ffreestanding -fno-builtin -fno-pic -fno-pie
                -fno-exceptions -fno-rtti -fno-threadsafe-statics
                -fno-stack-protector -fno-omit-frame-pointer -mno-red-zone
                -mcmodel=large -fno-asynchronous-unwind-tables -fno-use-cxa-atexit
                -Wall -Wextra -Werror -pedantic
                -I${ASTIA_KERNEL_SOURCE_DIR}/include
                -I${CMAKE_CURRENT_SOURCE_DIR}
                -c ${CMAKE_CURRENT_SOURCE_DIR}/rtl8139.cpp -o ${ASTIA_RTL8139_OBJECT}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/rtl8139.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/rtl8139_protocol.hpp
                ${ASTIA_RTL8139_ABI_HEADERS}
        COMMENT "Compiling RTL8139 loadable module"
        VERBATIM)

add_custom_target(rtl8139_module ALL DEPENDS ${ASTIA_RTL8139_OBJECT})

add_custom_command(
        OUTPUT ${ASTIA_RTL8139_UNIT}
        COMMAND ${CMAKE_CXX_COMPILER} -std=c++20 -Wall -Wextra -Werror -pedantic
                -I${ASTIA_KERNEL_SOURCE_DIR}/include
                -I${CMAKE_CURRENT_SOURCE_DIR}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/rtl8139_protocol_test.cpp
                -o ${ASTIA_RTL8139_UNIT}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/rtl8139_protocol_test.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/rtl8139_protocol.hpp
                ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/module_abi_core.h
        VERBATIM)

add_custom_target(rtl8139_unit_target ALL DEPENDS ${ASTIA_RTL8139_UNIT})

if(ASTIA_BUILD_TESTS)
    add_test(NAME rtl8139_protocol_unit COMMAND ${ASTIA_RTL8139_UNIT})
endif()

add_custom_target(rtl8139_qemu_probe_smoke
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run-qemu.sh
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        VERBATIM)

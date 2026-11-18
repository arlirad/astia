set(ASTIA_USB_OBJECT ${CMAKE_CURRENT_BINARY_DIR}/usb.o)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/AstiaCTest.cmake)
set(ASTIA_USB_UNIT ${CMAKE_CURRENT_BINARY_DIR}/usb_unit)

set(ASTIA_USB_ABI_HEADERS
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/module_abi_core.h
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/module_abi_pci.h
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/module_abi_usb.h
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/module_abi_memory.h
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/module_abi_io.h
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/module_abi_interrupt.h
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/module_abi.h)

add_custom_command(
        OUTPUT ${ASTIA_USB_OBJECT}
        COMMAND ${CMAKE_CXX_COMPILER}
                -std=c++20 -O2 -ffreestanding -fno-builtin -fno-pic -fno-pie
                -fno-exceptions -fno-rtti -fno-threadsafe-statics
                -fno-stack-protector -fno-omit-frame-pointer -mno-red-zone
                -mcmodel=large -fno-asynchronous-unwind-tables -fno-use-cxa-atexit
                -Wall -Wextra -Werror -pedantic
                -I${ASTIA_KERNEL_SOURCE_DIR}/include
                -I${CMAKE_CURRENT_SOURCE_DIR}
                -c ${CMAKE_CURRENT_SOURCE_DIR}/xhci.cpp -o ${ASTIA_USB_OBJECT}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/xhci.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/usb_protocol.hpp
                ${CMAKE_CURRENT_SOURCE_DIR}/xhci.hpp
                ${ASTIA_USB_ABI_HEADERS}
        COMMENT "Compiling USB xHCI foundation loadable module"
        VERBATIM)

add_custom_target(usb_module ALL DEPENDS ${ASTIA_USB_OBJECT})

add_custom_command(
        OUTPUT ${ASTIA_USB_UNIT}
        COMMAND ${CMAKE_CXX_COMPILER} -std=c++20 -Wall -Wextra -Werror -pedantic
                ${ASTIA_HOST_SANITIZER_COMPILE_OPTIONS}
                -I${CMAKE_CURRENT_SOURCE_DIR}
                -I${ASTIA_KERNEL_SOURCE_DIR}/include
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/usb_protocol_test.cpp
                -o ${ASTIA_USB_UNIT}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/usb_protocol_test.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/usb_protocol.hpp
                ${CMAKE_CURRENT_SOURCE_DIR}/xhci.hpp
        VERBATIM)

add_custom_target(usb_unit_target ALL DEPENDS ${ASTIA_USB_UNIT})

if(ASTIA_BUILD_TESTS)
    add_test(NAME usb_protocol_unit COMMAND ${ASTIA_USB_UNIT})
    astia_append_test_label("module;usb;xhci;host;deterministic")
    add_test(NAME usb_xhci_qemu_enumeration
            COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run-qemu.sh)
    set_tests_properties(usb_xhci_qemu_enumeration PROPERTIES
            LABELS "module;usb;xhci;qemu;deterministic")
endif()

add_custom_target(usb_qemu_capability_smoke
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run-qemu.sh
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        VERBATIM)

add_custom_target(usb_qemu_keyboard_vga_smoke
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run-keyboard-vga-smoke.sh
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        VERBATIM)

add_custom_target(usb_qemu_enumeration_smoke
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run-qemu.sh
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        VERBATIM)

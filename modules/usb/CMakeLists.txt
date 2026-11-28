set(ASTIA_USB_OBJECT ${CMAKE_CURRENT_BINARY_DIR}/usb.km)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/AstiaCTest.cmake)
set(ASTIA_USB_UNIT ${CMAKE_CURRENT_BINARY_DIR}/usb_unit)
set(ASTIA_USB_MMIO_WIDTH_UNIT ${CMAKE_CURRENT_BINARY_DIR}/usb_mmio_width_unit)

set(ASTIA_USB_ABI_HEADERS
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/module.hpp)

add_custom_command(
        OUTPUT ${ASTIA_USB_OBJECT}
        COMMAND ${CMAKE_CXX_COMPILER}
                -std=c++20 -O2 -ffreestanding -fno-builtin -fno-pic -fno-pie
                -fno-exceptions -fno-rtti -fno-threadsafe-statics
                -fno-stack-protector -fno-omit-frame-pointer -mno-red-zone
                -mcmodel=large -fno-asynchronous-unwind-tables -fno-use-cxa-atexit
                -DASTIA_MODULE_ET_REL=1
                -Wall -Wextra -Werror -pedantic
                -I${ASTIA_KERNEL_SOURCE_DIR}/include
                -I${ASTIA_ARCH_INCLUDE_DIR}
                -I${CMAKE_CURRENT_SOURCE_DIR}
                -c ${CMAKE_CURRENT_SOURCE_DIR}/xhci.cpp -o ${ASTIA_USB_OBJECT}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/xhci.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/usb_protocol.hpp
                ${CMAKE_CURRENT_SOURCE_DIR}/xhci.hpp
                ${CMAKE_CURRENT_SOURCE_DIR}/mmio_width.hpp
                ${ASTIA_USB_ABI_HEADERS}
        COMMENT "Compiling USB xHCI foundation loadable module"
        VERBATIM)

add_custom_target(usb_module ALL DEPENDS ${ASTIA_USB_OBJECT})

set(ASTIA_USB_FAILURE_OBJECT ${CMAKE_CURRENT_BINARY_DIR}/usb-failure.km)
add_custom_command(
        OUTPUT ${ASTIA_USB_FAILURE_OBJECT}
        COMMAND ${CMAKE_CXX_COMPILER}
                -DASTIA_USB_TEST_BAR_MAP_FAILURE=1
                -std=c++20 -O2 -ffreestanding -fno-builtin -fno-pic -fno-pie
                -fno-exceptions -fno-rtti -fno-threadsafe-statics
                -fno-stack-protector -fno-omit-frame-pointer -mno-red-zone
                -mcmodel=large -fno-asynchronous-unwind-tables
                -DASTIA_MODULE_ET_REL=1
                -Wall -Wextra -Werror -pedantic
                -I${ASTIA_KERNEL_SOURCE_DIR}/include
                -I${ASTIA_ARCH_INCLUDE_DIR}
                -I${CMAKE_CURRENT_SOURCE_DIR}
                -c ${CMAKE_CURRENT_SOURCE_DIR}/xhci.cpp
                -o ${ASTIA_USB_FAILURE_OBJECT}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/xhci.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/usb_protocol.hpp
                ${CMAKE_CURRENT_SOURCE_DIR}/xhci.hpp
                ${CMAKE_CURRENT_SOURCE_DIR}/mmio_width.hpp
                ${ASTIA_USB_ABI_HEADERS}
        COMMENT "Compiling USB xHCI rollback test module"
        VERBATIM)
add_custom_target(usb_failure_module DEPENDS ${ASTIA_USB_FAILURE_OBJECT})

add_custom_command(
        OUTPUT ${ASTIA_USB_UNIT}
        COMMAND ${CMAKE_CXX_COMPILER} -std=c++20 -Wall -Wextra -Werror -pedantic
                ${ASTIA_HOST_SANITIZER_COMPILE_OPTIONS}
                -I${CMAKE_CURRENT_SOURCE_DIR}
                -I${ASTIA_KERNEL_SOURCE_DIR}/include
                -I${ASTIA_ARCH_INCLUDE_DIR}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/usb_protocol_test.cpp
                -o ${ASTIA_USB_UNIT}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/usb_protocol_test.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/usb_protocol.hpp
                ${CMAKE_CURRENT_SOURCE_DIR}/xhci.hpp
        VERBATIM)

add_custom_target(usb_unit_target ALL DEPENDS ${ASTIA_USB_UNIT})

add_custom_command(
        OUTPUT ${ASTIA_USB_MMIO_WIDTH_UNIT}
        COMMAND ${CMAKE_CXX_COMPILER} -std=c++20 -Wall -Wextra -Werror -pedantic
                ${ASTIA_HOST_SANITIZER_COMPILE_OPTIONS}
                -I${CMAKE_CURRENT_SOURCE_DIR}
                -I${ASTIA_KERNEL_SOURCE_DIR}/include
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/mmio_width_test.cpp
                -o ${ASTIA_USB_MMIO_WIDTH_UNIT}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/mmio_width_test.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/mmio_width.hpp
        VERBATIM)

add_custom_target(usb_mmio_width_unit_target ALL DEPENDS ${ASTIA_USB_MMIO_WIDTH_UNIT})
add_dependencies(usb_unit_target usb_mmio_width_unit_target)

if(ASTIA_BUILD_TESTS)
    add_test(NAME usb_protocol_unit COMMAND ${ASTIA_USB_UNIT})
    astia_append_test_label("module;usb;xhci;host;deterministic")
    astia_add_test_label(usb_protocol_unit "sanitizer-host")
    add_test(NAME usb_mmio_width_unit COMMAND ${ASTIA_USB_MMIO_WIDTH_UNIT})
    astia_add_test_label(usb_mmio_width_unit "module;usb;xhci;host;deterministic")
    astia_add_test_label(usb_mmio_width_unit "sanitizer-host")
    add_test(NAME usb_xhci_probe_stack_usage
            COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests/probe_stack_usage.py
                    ${ASTIA_USB_OBJECT})
    set_tests_properties(usb_xhci_probe_stack_usage PROPERTIES
            LABELS "module;usb;xhci;host;deterministic"
            FIXTURES_REQUIRED usb-module-artifacts)
    astia_add_test_label(usb_xhci_probe_stack_usage "sanitizer-host-support")
    add_test(NAME usb_module_prepare
            COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
                    --target usb_module)
    set_tests_properties(usb_module_prepare PROPERTIES
            LABELS "build;module;usb;xhci;deterministic"
            FIXTURES_SETUP usb-module-artifacts
            RESOURCE_LOCK astia-build-tree
            TIMEOUT 300)
    astia_add_test_label(usb_module_prepare "sanitizer-host-support")
    add_test(NAME usb_xhci_stop_quarantine_policy
            COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests/stop_quarantine_policy.py
                    ${CMAKE_CURRENT_SOURCE_DIR}/xhci.cpp)
    set_tests_properties(usb_xhci_stop_quarantine_policy PROPERTIES
            LABELS "module;usb;xhci;host;deterministic")
    astia_add_test_label(usb_xhci_stop_quarantine_policy "sanitizer-host-support")
    add_test(NAME usb_xhci_qemu_enumeration
            COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run-qemu.sh)
    set_tests_properties(usb_xhci_qemu_enumeration PROPERTIES
            LABELS "module;usb;xhci;qemu;deterministic"
            ENVIRONMENT "ASTIA_TEST_BUILD=${CMAKE_BINARY_DIR}"
            FIXTURES_REQUIRED astia-qemu-artifacts
            RESOURCE_LOCK "astia-qemu;astia-build-tree")
    add_test(NAME usb_xhci_qemu_rollback
            COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run-qemu.sh)
    set_tests_properties(usb_xhci_qemu_rollback PROPERTIES
            LABELS "module;usb;xhci;qemu;deterministic"
            ENVIRONMENT
            "ASTIA_TEST_BUILD=${CMAKE_BINARY_DIR};ASTIA_USB_ROLLBACK_TEST=1;ASTIA_USB_MODULE=${CMAKE_CURRENT_BINARY_DIR}/usb-failure.km;ASTIA_USB_MODULE_TARGET=usb_failure_module"
            FIXTURES_REQUIRED astia-qemu-artifacts
            RESOURCE_LOCK "astia-qemu;astia-build-tree")
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

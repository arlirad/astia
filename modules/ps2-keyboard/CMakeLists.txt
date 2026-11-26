include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/AstiaCTest.cmake)

set(ASTIA_PS2_SOURCES
        ps2_controller.cpp
        scancode.cpp
        keymap.cpp
        ps2_keyboard.cpp
        module_main.cpp)

set(ASTIA_PS2_ABI_HEADERS
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/module.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/dev/capabilities.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/dev/tty_input.hpp)

set(ASTIA_PS2_LOCAL_HEADERS
        ${CMAKE_CURRENT_SOURCE_DIR}/ps2_controller.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/scancode.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/keymap.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ps2_keyboard.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/ps2_fake_8042.hpp)

set(ASTIA_PS2_OBJECT ${CMAKE_CURRENT_BINARY_DIR}/ps2-keyboard.o)

set(ASTIA_PS2_OBJECTS)
foreach(_astia_ps2_source IN LISTS ASTIA_PS2_SOURCES)
    get_filename_component(_astia_ps2_name ${_astia_ps2_source} NAME_WE)
    set(_astia_ps2_object ${CMAKE_CURRENT_BINARY_DIR}/${_astia_ps2_name}.module.o)
    add_custom_command(
            OUTPUT ${_astia_ps2_object}
            COMMAND ${CMAKE_CXX_COMPILER} -std=c++20 -O2 -c
                    -ffreestanding -fno-builtin -fno-pic -fno-pie
                    -fno-exceptions -fno-rtti -fno-threadsafe-statics
                    -fno-stack-protector -fno-omit-frame-pointer -mno-red-zone
                    -mcmodel=large -fno-asynchronous-unwind-tables
                    -fno-use-cxa-atexit -Wall -Wextra -Werror -pedantic
                    -MMD -MF ${_astia_ps2_object}.d -MT ${_astia_ps2_object}
                    -I${ASTIA_KERNEL_SOURCE_DIR}/include
                    -I${ASTIA_ARCH_INCLUDE_DIR}
                    -I${CMAKE_CURRENT_SOURCE_DIR}
                    -c ${CMAKE_CURRENT_SOURCE_DIR}/${_astia_ps2_source}
                    -o ${_astia_ps2_object}
            DEPFILE ${_astia_ps2_object}.d
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${_astia_ps2_source}
                    ${ASTIA_PS2_ABI_HEADERS}
                    ${ASTIA_PS2_LOCAL_HEADERS}
            COMMENT "Compiling PS/2 keyboard module ${_astia_ps2_source}"
            VERBATIM)
    list(APPEND ASTIA_PS2_OBJECTS ${_astia_ps2_object})
endforeach()

add_custom_command(
        OUTPUT ${ASTIA_PS2_OBJECT}
        COMMAND ${CMAKE_CXX_COMPILER} -nostdlib -r ${ASTIA_PS2_OBJECTS}
                -o ${ASTIA_PS2_OBJECT}
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/check-imports.sh ${ASTIA_PS2_OBJECT}
        DEPENDS ${ASTIA_PS2_OBJECTS}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/check-imports.sh
        COMMENT "Linking PS/2 keyboard loadable module"
        VERBATIM)
add_custom_target(ps2_keyboard_module ALL DEPENDS ${ASTIA_PS2_OBJECT})

function(astia_ps2_add_unit name)
    set(_output ${CMAKE_CURRENT_BINARY_DIR}/${name})
    add_custom_command(
            OUTPUT ${_output}
            COMMAND ${CMAKE_CXX_COMPILER} -std=c++20 -Wall -Wextra -Werror -pedantic
                    -fno-exceptions -fno-rtti
                    ${ASTIA_HOST_SANITIZER_COMPILE_OPTIONS}
                    -MMD -MF ${_output}.d -MT ${_output}
                    -I${ASTIA_KERNEL_SOURCE_DIR}/include
                    -I${ASTIA_ARCH_INCLUDE_DIR}
                    -I${CMAKE_CURRENT_SOURCE_DIR}
                    -I${ASTIA_KERNEL_SOURCE_DIR}/tests
                    -I${ASTIA_KERNEL_SOURCE_DIR}/tests/support
                    ${ARGN}
                    ${ASTIA_KERNEL_SOURCE_DIR}/tests/support/main.cpp
                    ${ASTIA_HOST_SANITIZER_LINK_OPTIONS}
                    -o ${_output}
            DEPFILE ${_output}.d
            DEPENDS ${ARGN}
                    ${ASTIA_KERNEL_SOURCE_DIR}/tests/support/main.cpp
                    ${ASTIA_KERNEL_SOURCE_DIR}/tests/support/test.hpp
                    ${ASTIA_PS2_ABI_HEADERS}
                    ${ASTIA_PS2_LOCAL_HEADERS}
            VERBATIM)
    add_custom_target(${name}_target ALL DEPENDS ${_output})
    if(ASTIA_BUILD_TESTS)
        add_test(NAME ${name} COMMAND ${_output})
        astia_add_test_label(${name} "module;ps2;input;host;deterministic")
        astia_add_test_label(${name} "sanitizer-host")
    endif()
endfunction()

astia_ps2_add_unit(ps2_controller_unit
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/ps2_controller_test.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ps2_controller.cpp)

astia_ps2_add_unit(ps2_scancode_unit
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/ps2_scancode_test.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/scancode.cpp)

astia_ps2_add_unit(ps2_keyboard_unit
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/ps2_keyboard_test.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ps2_controller.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/scancode.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/keymap.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ps2_keyboard.cpp)
add_dependencies(ps2_keyboard_unit_target ps2_keyboard_module)

find_program(ASTIA_PS2_QEMU qemu-system-x86_64)
find_program(ASTIA_PS2_CPIO cpio)
find_program(ASTIA_PS2_GRUB_FILE grub-file)
find_program(ASTIA_PS2_GRUB_MKRESCUE grub-mkrescue)

set(ASTIA_PS2_ISO ${CMAKE_CURRENT_BINARY_DIR}/astia-ps2-keyboard.iso)

if(ASTIA_PS2_CPIO AND ASTIA_PS2_GRUB_FILE AND ASTIA_PS2_GRUB_MKRESCUE
        AND TARGET astiakrn_runtime)
    add_custom_command(
            OUTPUT ${ASTIA_PS2_ISO}
            COMMAND ${CMAKE_COMMAND} -E env
                    BUILD=${CMAKE_CURRENT_BINARY_DIR}
                    ISO=${ASTIA_PS2_ISO}
                    KERNEL=${CMAKE_BINARY_DIR}/kernel/src/astiakrn_runtime
                    MODULE=${ASTIA_PS2_OBJECT}
                    SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
                    INCLUDE_DIR=${ASTIA_KERNEL_SOURCE_DIR}/include
                    CC=${CMAKE_C_COMPILER}
                    CPIO=${ASTIA_PS2_CPIO}
                    GRUB_FILE=${ASTIA_PS2_GRUB_FILE}
                    GRUB_MKRESCUE=${ASTIA_PS2_GRUB_MKRESCUE}
                    sh ${CMAKE_CURRENT_SOURCE_DIR}/make-grub-iso.sh
            DEPENDS ${ASTIA_PS2_OBJECT}
                    ${CMAKE_CURRENT_SOURCE_DIR}/make-grub-iso.sh
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests/ps2_ttv0_probe.c
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests/ps2_noop.c
                    astiakrn_runtime
            COMMENT "Building PS/2 keyboard boot ISO"
            VERBATIM)
    add_custom_target(astia_ps2_keyboard_iso ALL DEPENDS ${ASTIA_PS2_ISO})
endif()

if(ASTIA_BUILD_TESTS)
    add_test(NAME ps2_keyboard_imports
            COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/check-imports.sh
                    ${ASTIA_PS2_OBJECT})
    set_tests_properties(ps2_keyboard_imports PROPERTIES
            LABELS "module;ps2;input;abi;deterministic"
            FIXTURES_REQUIRED astia-module-host)

    if(ASTIA_PS2_QEMU AND TARGET astia_ps2_keyboard_iso)
        add_test(NAME astia_ps2_keyboard_qemu
                COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run-qemu-load.sh
                        ${ASTIA_PS2_QEMU}
                        ${ASTIA_PS2_ISO}
                        ${CMAKE_CURRENT_BINARY_DIR}/ps2-keyboard-serial.log)
        set_tests_properties(astia_ps2_keyboard_qemu PROPERTIES
                LABELS "module;ps2;input;qemu;boot;deterministic"
                TIMEOUT 90
                FIXTURES_REQUIRED astia-qemu-artifacts
                RESOURCE_LOCK astia-qemu)
    endif()
endif()

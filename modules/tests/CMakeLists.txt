add_library(astia_module_fixtures INTERFACE)

target_include_directories(astia_module_fixtures INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../kernel/include)

set(ASTIA_MODULE_ABI_C_OBJECT ${CMAKE_CURRENT_BINARY_DIR}/module_abi_c_module.o)
add_custom_command(
        OUTPUT ${ASTIA_MODULE_ABI_C_OBJECT}
        COMMAND ${CMAKE_C_COMPILER} -std=c11 -Wall -Wextra -Werror -pedantic
                -ffreestanding -fno-pic -fno-pie
                -I${ASTIA_KERNEL_SOURCE_DIR}/include
                -c ${CMAKE_CURRENT_SOURCE_DIR}/module_abi_c_module.c
                -o ${ASTIA_MODULE_ABI_C_OBJECT}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/module_abi_c_module.c
                ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/module_abi.h
        VERBATIM)
add_custom_target(astia_module_c_compatibility ALL DEPENDS ${ASTIA_MODULE_ABI_C_OBJECT})

set(ASTIA_MODULE_ABI_LAYOUT ${CMAKE_CURRENT_BINARY_DIR}/module_abi_layout)
add_custom_command(
        OUTPUT ${ASTIA_MODULE_ABI_LAYOUT}
        COMMAND ${CMAKE_CXX_COMPILER} -std=c++20 -Wall -Wextra -Werror -pedantic
                -I${ASTIA_KERNEL_SOURCE_DIR}/include
                ${CMAKE_CURRENT_SOURCE_DIR}/module_abi_layout.cpp
                -o ${ASTIA_MODULE_ABI_LAYOUT}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/module_abi_layout.cpp
                ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/module_abi.h
        VERBATIM)
add_custom_target(astia_module_abi_layout_target ALL DEPENDS ${ASTIA_MODULE_ABI_LAYOUT})
add_test(NAME astia_module_abi_layout COMMAND ${ASTIA_MODULE_ABI_LAYOUT})

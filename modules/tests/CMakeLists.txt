add_library(astia_module_fixtures INTERFACE)

target_include_directories(astia_module_fixtures INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../kernel/include)

set(ASTIA_MODULE_ABI_HEADERS
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/module_abi_core.h
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/module_abi_pci.h
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/module_abi_memory.h
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/module_abi_io.h
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/module_abi_interrupt.h
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/module_abi.h)
set(ASTIA_MODULE_ABI_CXX_HEADERS
        ${ASTIA_MODULE_ABI_HEADERS}
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/module_abi.hpp)

set(ASTIA_MODULE_ABI_HEADER_CXX_OBJECTS)
foreach(ASTIA_MODULE_ABI_HEADER IN LISTS ASTIA_MODULE_ABI_CXX_HEADERS)
    get_filename_component(ASTIA_MODULE_ABI_HEADER_NAME
            ${ASTIA_MODULE_ABI_HEADER} NAME)
    string(REPLACE "." "_" ASTIA_MODULE_ABI_HEADER_NAME
            ${ASTIA_MODULE_ABI_HEADER_NAME})
    set(ASTIA_MODULE_ABI_HEADER_CXX_OBJECT
            ${CMAKE_CURRENT_BINARY_DIR}/${ASTIA_MODULE_ABI_HEADER_NAME}_standalone_cxx.o)
    add_custom_command(
            OUTPUT ${ASTIA_MODULE_ABI_HEADER_CXX_OBJECT}
            COMMAND ${CMAKE_CXX_COMPILER} -std=c++20 -Wall -Wextra -Werror -pedantic
                    -I${ASTIA_KERNEL_SOURCE_DIR}/include
                    -include ${ASTIA_MODULE_ABI_HEADER}
                    -c ${CMAKE_CURRENT_SOURCE_DIR}/module_abi_header_alone.cpp
                    -o ${ASTIA_MODULE_ABI_HEADER_CXX_OBJECT}
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/module_abi_header_alone.cpp
                    ${ASTIA_MODULE_ABI_HEADER}
            VERBATIM)
    list(APPEND ASTIA_MODULE_ABI_HEADER_CXX_OBJECTS ${ASTIA_MODULE_ABI_HEADER_CXX_OBJECT})
endforeach()

add_custom_target(astia_module_abi_header_alone ALL
        DEPENDS ${ASTIA_MODULE_ABI_HEADER_CXX_OBJECTS})

set(ASTIA_MODULE_ABI_C_OBJECT ${CMAKE_CURRENT_BINARY_DIR}/module_abi_c_module.o)
add_custom_command(
        OUTPUT ${ASTIA_MODULE_ABI_C_OBJECT}
        COMMAND ${CMAKE_C_COMPILER} -std=c11 -Wall -Wextra -Werror -pedantic
                -ffreestanding -fno-pic -fno-pie
                -I${ASTIA_KERNEL_SOURCE_DIR}/include
                -c ${CMAKE_CURRENT_SOURCE_DIR}/module_abi_c_module.c
                -o ${ASTIA_MODULE_ABI_C_OBJECT}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/module_abi_c_module.c
                ${ASTIA_MODULE_ABI_HEADERS}
        VERBATIM)
add_custom_target(astia_module_c_compatibility ALL DEPENDS ${ASTIA_MODULE_ABI_C_OBJECT})

set(ASTIA_MODULE_ABI_CPP_OBJECT ${CMAKE_CURRENT_BINARY_DIR}/module_abi_cpp_module.o)
add_custom_command(
        OUTPUT ${ASTIA_MODULE_ABI_CPP_OBJECT}
        COMMAND ${CMAKE_CXX_COMPILER} -std=c++20 -Wall -Wextra -Werror -pedantic
                -ffreestanding -fno-pic -fno-pie -fno-exceptions -fno-rtti
                -fno-threadsafe-statics -fno-stack-protector -fno-omit-frame-pointer
                -mcmodel=large -fno-asynchronous-unwind-tables
                -I${ASTIA_KERNEL_SOURCE_DIR}/include
                -c ${CMAKE_CURRENT_SOURCE_DIR}/module_abi_cpp_module.cpp
                -o ${ASTIA_MODULE_ABI_CPP_OBJECT}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/module_abi_cpp_module.cpp
                ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/module_abi.hpp
                ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/module_abi.h
        VERBATIM)
add_custom_target(astia_module_cpp_compatibility ALL DEPENDS ${ASTIA_MODULE_ABI_CPP_OBJECT})

set(ASTIA_MODULE_ABI_LAYOUT ${CMAKE_CURRENT_BINARY_DIR}/module_abi_layout)
add_custom_command(
        OUTPUT ${ASTIA_MODULE_ABI_LAYOUT}
        COMMAND ${CMAKE_CXX_COMPILER} -std=c++20 -Wall -Wextra -Werror -pedantic
                -I${ASTIA_KERNEL_SOURCE_DIR}/include
                ${CMAKE_CURRENT_SOURCE_DIR}/module_abi_layout.cpp
                -o ${ASTIA_MODULE_ABI_LAYOUT}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/module_abi_layout.cpp
                ${ASTIA_MODULE_ABI_HEADERS}
                ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/module_abi.hpp
        VERBATIM)
add_custom_target(astia_module_abi_layout_target ALL DEPENDS ${ASTIA_MODULE_ABI_LAYOUT})
if(ASTIA_BUILD_TESTS)
    add_test(NAME astia_module_abi_layout COMMAND ${ASTIA_MODULE_ABI_LAYOUT})
endif()

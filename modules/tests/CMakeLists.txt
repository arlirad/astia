add_library(astia_module_fixtures INTERFACE)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/AstiaCTest.cmake)

target_include_directories(astia_module_fixtures INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../kernel/include)

set(ASTIA_TYPED_HEADERS
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/module.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/io.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/mm/dma_buffer.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/dev/blk/blkdev.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/dev/chr/chrdev.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/hw/driver.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/strings.hpp)
set(ASTIA_TYPED_HEADER_OBJECTS)
foreach(ASTIA_TYPED_HEADER IN LISTS ASTIA_TYPED_HEADERS)
    get_filename_component(ASTIA_TYPED_HEADER_NAME ${ASTIA_TYPED_HEADER} NAME)
    string(REPLACE "." "_" ASTIA_TYPED_HEADER_NAME ${ASTIA_TYPED_HEADER_NAME})
    set(ASTIA_TYPED_HEADER_OBJECT
            ${CMAKE_CURRENT_BINARY_DIR}/${ASTIA_TYPED_HEADER_NAME}_typed_standalone_cxx.o)
    add_custom_command(
            OUTPUT ${ASTIA_TYPED_HEADER_OBJECT}
            COMMAND ${CMAKE_CXX_COMPILER} -std=c++20 -Wall -Wextra -Werror -pedantic
                    -I${ASTIA_KERNEL_SOURCE_DIR}/include
                    -include ${ASTIA_TYPED_HEADER}
                    -c ${CMAKE_CURRENT_SOURCE_DIR}/typed_header_alone.cpp
                    -o ${ASTIA_TYPED_HEADER_OBJECT}
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/typed_header_alone.cpp
                    ${ASTIA_TYPED_HEADER}
            VERBATIM)
    list(APPEND ASTIA_TYPED_HEADER_OBJECTS ${ASTIA_TYPED_HEADER_OBJECT})
endforeach()
add_custom_target(astia_typed_header_alone ALL DEPENDS ${ASTIA_TYPED_HEADER_OBJECTS})

set(ASTIA_TYPED_MODULE_OBJECT ${CMAKE_CURRENT_BINARY_DIR}/typed_block_module.o)
add_custom_command(
        OUTPUT ${ASTIA_TYPED_MODULE_OBJECT}
        COMMAND ${CMAKE_CXX_COMPILER} -std=c++20 -Wall -Wextra -Werror -pedantic
                -ffreestanding -fno-pic -fno-pie -fno-exceptions -fno-rtti
                -fno-threadsafe-statics -fno-stack-protector -fno-omit-frame-pointer
                -mcmodel=large -fno-asynchronous-unwind-tables
                -DASTIA_MODULE_ET_REL=1
                -I${ASTIA_KERNEL_SOURCE_DIR}/include
                -c ${CMAKE_CURRENT_SOURCE_DIR}/typed_block_module.cpp
                -o ${ASTIA_TYPED_MODULE_OBJECT}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/typed_block_module.cpp
                ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/core/task.hpp
                ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/core/resume_queue.hpp
                ${ASTIA_TYPED_HEADERS}
        VERBATIM)
add_custom_target(astia_typed_module ALL DEPENDS ${ASTIA_TYPED_MODULE_OBJECT})

# A module may include the port-owned context classes.  That view is the
# generic include root plus the build-selected port overlay, exactly what the
# module SDK ships for the target.
set(ASTIA_ARCH_CONTEXT_MODULE_OBJECT
        ${CMAKE_CURRENT_BINARY_DIR}/arch_context_module.o)
add_custom_command(
        OUTPUT ${ASTIA_ARCH_CONTEXT_MODULE_OBJECT}
        COMMAND ${CMAKE_CXX_COMPILER} -std=c++20 -Wall -Wextra -Werror -pedantic
                -ffreestanding -fno-pic -fno-pie -fno-exceptions -fno-rtti
                -fno-threadsafe-statics -fno-stack-protector -fno-omit-frame-pointer
                -mcmodel=large -fno-asynchronous-unwind-tables
                -DASTIA_MODULE_ET_REL=1
                -I${ASTIA_KERNEL_SOURCE_DIR}/include
                -I${ASTIA_ARCH_INCLUDE_DIR}
                -c ${CMAKE_CURRENT_SOURCE_DIR}/arch_context_module.cpp
                -o ${ASTIA_ARCH_CONTEXT_MODULE_OBJECT}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/arch_context_module.cpp
                ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/arch/context.hpp
                ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/arch/interrupts.hpp
                ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/arch/signal.hpp
                ${ASTIA_ARCH_INCLUDE_DIR}/astia/arch/syscall_image.hpp
                ${ASTIA_ARCH_INCLUDE_DIR}/astia/arch/interrupt_frame.hpp
        VERBATIM)
add_custom_target(astia_arch_context_module ALL
        DEPENDS ${ASTIA_ARCH_CONTEXT_MODULE_OBJECT})
if(ASTIA_BUILD_TESTS)
    add_test(NAME astia_arch_context_module_build
             COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
                     --target astia_arch_context_module)
    set_tests_properties(astia_arch_context_module_build
            PROPERTIES RESOURCE_LOCK astia-build-tree)
    astia_add_test_label(astia_arch_context_module_build
            "module;abi;deterministic")
endif()

set(ASTIA_TYPED_LAYOUT ${CMAKE_CURRENT_BINARY_DIR}/typed_contract_layout)
add_custom_command(
        OUTPUT ${ASTIA_TYPED_LAYOUT}
        COMMAND ${CMAKE_CXX_COMPILER} -std=c++20 -Wall -Wextra -Werror -pedantic
                ${ASTIA_HOST_SANITIZER_COMPILE_OPTIONS}
                -I${ASTIA_KERNEL_SOURCE_DIR}/include
                ${CMAKE_CURRENT_SOURCE_DIR}/typed_contract_layout.cpp
                ${ASTIA_HOST_SANITIZER_LINK_OPTIONS}
                -o ${ASTIA_TYPED_LAYOUT}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/typed_contract_layout.cpp
                ${ASTIA_TYPED_HEADERS}
        VERBATIM)
add_custom_target(astia_typed_contract_layout_target ALL DEPENDS ${ASTIA_TYPED_LAYOUT})
if(ASTIA_BUILD_TESTS)
    add_test(NAME astia_typed_contract_layout COMMAND ${ASTIA_TYPED_LAYOUT})
    astia_add_test_label(astia_typed_contract_layout "sanitizer-host")
endif()

astia_append_test_label("module;abi;deterministic")

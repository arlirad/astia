add_library(astia_module_fixtures INTERFACE)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/AstiaCTest.cmake)

target_include_directories(astia_module_fixtures INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../kernel/include)

set(ASTIA_TYPED_HEADERS
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/io.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/mm/dma_buffer.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/dev/blk/blkdev.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/dev/chr/chrdev.hpp)
list(APPEND ASTIA_TYPED_HEADERS
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/hw/driver.hpp)
set(ASTIA_TYPED_HEADER_OBJECTS)
foreach(ASTIA_TYPED_HEADER IN LISTS ASTIA_TYPED_HEADERS)
    get_filename_component(ASTIA_TYPED_HEADER_NAME ${ASTIA_TYPED_HEADER} NAME)
    string(REPLACE "." "_" ASTIA_TYPED_HEADER_NAME ${ASTIA_TYPED_HEADER_NAME})
    set(ASTIA_TYPED_HEADER_OBJECT
            ${CMAKE_CURRENT_BINARY_DIR}/${ASTIA_TYPED_HEADER_NAME}_standalone_cxx.o)
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
                -I${ASTIA_KERNEL_SOURCE_DIR}/include
                -c ${CMAKE_CURRENT_SOURCE_DIR}/typed_block_module.cpp
                -o ${ASTIA_TYPED_MODULE_OBJECT}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/typed_block_module.cpp
                ${ASTIA_TYPED_HEADERS}
        VERBATIM)
add_custom_target(astia_typed_module ALL DEPENDS ${ASTIA_TYPED_MODULE_OBJECT})

set(ASTIA_TYPED_LAYOUT ${CMAKE_CURRENT_BINARY_DIR}/typed_contract_layout)
add_custom_command(
        OUTPUT ${ASTIA_TYPED_LAYOUT}
        COMMAND ${CMAKE_CXX_COMPILER} -std=c++20 -Wall -Wextra -Werror -pedantic
                -I${ASTIA_KERNEL_SOURCE_DIR}/include
                ${CMAKE_CURRENT_SOURCE_DIR}/typed_contract_layout.cpp
                -o ${ASTIA_TYPED_LAYOUT}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/typed_contract_layout.cpp
                ${ASTIA_TYPED_HEADERS}
        VERBATIM)
add_custom_target(astia_typed_contract_layout_target ALL DEPENDS ${ASTIA_TYPED_LAYOUT})
if(ASTIA_BUILD_TESTS)
    add_test(NAME astia_typed_contract_layout COMMAND ${ASTIA_TYPED_LAYOUT})
endif()

astia_append_test_label("module;typed;deterministic")

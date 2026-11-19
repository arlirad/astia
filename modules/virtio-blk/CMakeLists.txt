set(ASTIA_VIRTIO_BLK_OBJECT ${CMAKE_CURRENT_BINARY_DIR}/virtio_blk.o)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/AstiaCTest.cmake)
enable_testing()

set(ASTIA_VIRTIO_BLK_UNIT ${CMAKE_CURRENT_BINARY_DIR}/virtio_blk_unit)
add_custom_command(
        OUTPUT ${ASTIA_VIRTIO_BLK_UNIT}
        COMMAND ${CMAKE_CXX_COMPILER} -std=c++20 -Wall -Wextra -pedantic
                ${ASTIA_HOST_SANITIZER_COMPILE_OPTIONS}
                -I${ASTIA_KERNEL_SOURCE_DIR}/include
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/virtio_blk_unit.cpp
                -o ${ASTIA_VIRTIO_BLK_UNIT}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/virtio_blk_unit.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/virtio_blk_protocol.hpp
                ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/module_abi_core.h
                ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/module_abi_interrupt.h
                ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/module_abi_io.h
                ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/module_abi_memory.h
                ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/module_abi_pci.h
                ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/mm/shptr.hpp
                ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/io.hpp
                ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/mm/dma_buffer.hpp
                ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/dev/blk/blkdev.hpp
                ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/hw/pci.hpp
        VERBATIM)
add_custom_target(virtio_blk_unit_target ALL DEPENDS ${ASTIA_VIRTIO_BLK_UNIT})
if(ASTIA_BUILD_TESTS)
    add_test(NAME virtio_blk_unit COMMAND ${ASTIA_VIRTIO_BLK_UNIT})
    astia_append_test_label("module;virtio;host;deterministic")
endif()

set(ASTIA_VIRTIO_BLK_TEARDOWN_UNIT ${CMAKE_CURRENT_BINARY_DIR}/virtio_blk_teardown_unit)
add_custom_command(
        OUTPUT ${ASTIA_VIRTIO_BLK_TEARDOWN_UNIT}
        COMMAND ${CMAKE_CXX_COMPILER}
                -std=c++20 -Wall -Wextra -Werror -pedantic
                -ffunction-sections -fdata-sections
                ${ASTIA_HOST_SANITIZER_COMPILE_OPTIONS}
                -I${ASTIA_KERNEL_SOURCE_DIR}/include
                -I${CMAKE_CURRENT_SOURCE_DIR}/../..
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/virtio_teardown_test.cpp
                -Wl,--gc-sections
                ${ASTIA_HOST_SANITIZER_LINK_OPTIONS}
                -o ${ASTIA_VIRTIO_BLK_TEARDOWN_UNIT}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/virtio_teardown_test.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/virtio_blk.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/virtio_blk_protocol.hpp
                ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/io.hpp
                ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/mm/dma_buffer.hpp
        VERBATIM)
add_custom_target(virtio_blk_teardown_target ALL DEPENDS ${ASTIA_VIRTIO_BLK_TEARDOWN_UNIT})
if(ASTIA_BUILD_TESTS)
    add_test(NAME virtio_blk_teardown_unit
             COMMAND ${ASTIA_VIRTIO_BLK_TEARDOWN_UNIT})
    astia_append_test_label("module;virtio;host;deterministic;teardown")
endif()

add_custom_command(
        OUTPUT ${ASTIA_VIRTIO_BLK_OBJECT}
        COMMAND ${CMAKE_CXX_COMPILER}
                -O2 -std=c++20 -ffreestanding -fno-builtin -fno-pic -fno-pie
                -fno-exceptions -fno-rtti -fno-threadsafe-statics
                -fno-stack-protector -fno-omit-frame-pointer -mno-red-zone
                -mcmodel=large -fno-asynchronous-unwind-tables
                -Wall -Wextra -Werror -pedantic
                -I${ASTIA_KERNEL_SOURCE_DIR}/include
                -c ${CMAKE_CURRENT_SOURCE_DIR}/virtio_blk.cpp -o ${ASTIA_VIRTIO_BLK_OBJECT}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/virtio_blk.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/virtio_blk_protocol.hpp
                ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/module_abi_core.h
                ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/module_abi_interrupt.h
                ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/module_abi_io.h
                ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/module_abi_memory.h
                ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/module_abi_pci.h
                ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/mm/shptr.hpp
                ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/io.hpp
                ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/mm/dma_buffer.hpp
                ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/dev/blk/blkdev.hpp
        COMMENT "Compiling virtio-blk loadable module"
        VERBATIM
)

add_custom_target(virtio_blk_module ALL DEPENDS ${ASTIA_VIRTIO_BLK_OBJECT})

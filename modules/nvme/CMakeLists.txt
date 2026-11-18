set(ASTIA_NVME_OBJECT ${CMAKE_CURRENT_BINARY_DIR}/nvme.o)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/AstiaCTest.cmake)

option(ASTIA_NVME_FORCE_POLLING "Build the NVMe fixture without MSI-X" OFF)
if (ASTIA_NVME_FORCE_POLLING)
    set(ASTIA_NVME_MODE -DASTIA_NVME_FORCE_POLLING=1)
else ()
    set(ASTIA_NVME_MODE)
endif ()

add_custom_command(
        OUTPUT ${ASTIA_NVME_OBJECT}
        COMMAND ${CMAKE_CXX_COMPILER}
                -std=c++20 -ffreestanding -fno-builtin -fno-pic -fno-pie
                -fno-exceptions -fno-rtti -fno-threadsafe-statics
                -fno-stack-protector -fno-omit-frame-pointer -mno-red-zone
                -mcmodel=large -fno-asynchronous-unwind-tables -fno-use-cxa-atexit
                -Wall -Wextra -Werror -pedantic
                ${ASTIA_NVME_MODE}
                -I${ASTIA_KERNEL_SOURCE_DIR}/include
                -c ${CMAKE_CURRENT_SOURCE_DIR}/nvme.cpp -o ${ASTIA_NVME_OBJECT}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/nvme.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/nvme_protocol.hpp
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
        COMMENT "Compiling NVMe loadable module"
        VERBATIM
)

add_custom_target(nvme_module ALL DEPENDS ${ASTIA_NVME_OBJECT})

add_custom_target(nvme_host_tests
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-host.sh
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../..
        VERBATIM)
if(ASTIA_BUILD_TESTS)
    add_test(NAME nvme_protocol_unit
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-host.sh)
    astia_append_test_label("module;nvme;host;deterministic")
endif()

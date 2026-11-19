set(ASTIA_NVME_OBJECT ${CMAKE_CURRENT_BINARY_DIR}/nvme.o)
set(ASTIA_NVME_MSIX_OBJECT ${CMAKE_CURRENT_BINARY_DIR}/nvme-msix.o)
set(ASTIA_NVME_POLLING_OBJECT ${CMAKE_CURRENT_BINARY_DIR}/nvme-polling.o)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/AstiaCTest.cmake)

option(ASTIA_NVME_FORCE_POLLING "Build the NVMe fixture without MSI-X" OFF)
if (ASTIA_NVME_FORCE_POLLING)
    set(ASTIA_NVME_MODE -DASTIA_NVME_FORCE_POLLING=1)
else ()
    set(ASTIA_NVME_MODE)
endif ()

set(ASTIA_NVME_COMMON_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/nvme.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/nvme_protocol.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/module.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/mm/shptr.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/io.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/mm/dma_buffer.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/dev/blk/blkdev.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/hw/pci.hpp)

function(astia_nvme_add_variant target output)
        add_custom_command(
                OUTPUT ${output}
                COMMAND ${CMAKE_CXX_COMPILER}
                        -std=c++20 -ffreestanding -fno-builtin -fno-pic -fno-pie
                        -fno-exceptions -fno-rtti -fno-threadsafe-statics
                        -fno-stack-protector -fno-omit-frame-pointer -mno-red-zone
                        -mcmodel=large -fno-asynchronous-unwind-tables -fno-use-cxa-atexit
                        -Wall -Wextra -Werror -pedantic
                        ${ARGN}
                        -I${ASTIA_KERNEL_SOURCE_DIR}/include
                        -c ${CMAKE_CURRENT_SOURCE_DIR}/nvme.cpp -o ${output}
                DEPENDS ${ASTIA_NVME_COMMON_DEPENDS}
                COMMENT "Compiling NVMe ${target} loadable module"
                VERBATIM)
        add_custom_target(${target} ALL DEPENDS ${output})
endfunction()

astia_nvme_add_variant(nvme_module ${ASTIA_NVME_OBJECT} ${ASTIA_NVME_MODE})
astia_nvme_add_variant(nvme_msix_module ${ASTIA_NVME_MSIX_OBJECT})
astia_nvme_add_variant(nvme_polling_module ${ASTIA_NVME_POLLING_OBJECT}
        -DASTIA_NVME_FORCE_POLLING=1)

add_custom_target(nvme_host_tests
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-host.sh
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../..
        VERBATIM)

set(ASTIA_NVME_TEARDOWN_UNIT ${CMAKE_CURRENT_BINARY_DIR}/nvme_teardown_unit)
add_custom_command(
        OUTPUT ${ASTIA_NVME_TEARDOWN_UNIT}
        COMMAND ${CMAKE_CXX_COMPILER}
                -std=c++20 -Wall -Wextra -Werror -pedantic
                -ffunction-sections -fdata-sections
                ${ASTIA_HOST_SANITIZER_COMPILE_OPTIONS}
                -I${ASTIA_KERNEL_SOURCE_DIR}/include
                -I${CMAKE_CURRENT_SOURCE_DIR}/../..
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/nvme_teardown_test.cpp
                -Wl,--gc-sections
                ${ASTIA_HOST_SANITIZER_LINK_OPTIONS}
                -o ${ASTIA_NVME_TEARDOWN_UNIT}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/nvme_teardown_test.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/nvme.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/nvme_protocol.hpp
                ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/io.hpp
                ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/mm/dma_buffer.hpp
        VERBATIM)
add_custom_target(nvme_teardown_target ALL DEPENDS ${ASTIA_NVME_TEARDOWN_UNIT})
if(ASTIA_BUILD_TESTS)
    add_test(NAME nvme_protocol_unit
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-host.sh)
    astia_append_test_label("module;nvme;host;deterministic")
    add_test(NAME nvme_teardown_unit COMMAND ${ASTIA_NVME_TEARDOWN_UNIT})
    astia_append_test_label("module;nvme;host;deterministic;teardown")
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/AstiaCTest.cmake)

option(ASTIA_NVME_FORCE_POLLING "Build the NVMe fixture without MSI-X" OFF)
set(ASTIA_NVME_COMMON_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/nvme.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/nvme_protocol.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/module_abi_core.h
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/module_abi_interrupt.h
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/module_abi_io.h
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/module_abi_memory.h
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/module_abi_pci.h
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/module_abi.hpp
        ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/mm/shptr.hpp)

function(astia_nvme_add_variant target output force_polling)
    if(force_polling)
        set(mode -DASTIA_NVME_FORCE_POLLING=1)
    else()
        set(mode)
    endif()
    add_custom_command(
            OUTPUT ${output}
            COMMAND ${CMAKE_CXX_COMPILER}
                    -std=c++20 -ffreestanding -fno-builtin -fno-pic -fno-pie
                    -fno-exceptions -fno-rtti -fno-threadsafe-statics
                    -fno-stack-protector -fno-omit-frame-pointer -mno-red-zone
                    -mcmodel=large -fno-asynchronous-unwind-tables -fno-use-cxa-atexit
                    -Wall -Wextra -Werror -pedantic
                    ${mode}
                    -I${ASTIA_KERNEL_SOURCE_DIR}/include
                    -c ${CMAKE_CURRENT_SOURCE_DIR}/nvme.cpp -o ${output}
            DEPENDS ${ASTIA_NVME_COMMON_DEPENDS}
            COMMENT "Compiling NVMe ${target} loadable module"
            VERBATIM)
    add_custom_target(${target} ALL DEPENDS ${output})
endfunction()

set(ASTIA_NVME_MSIX_OBJECT ${CMAKE_CURRENT_BINARY_DIR}/nvme-msix.o)
set(ASTIA_NVME_POLLING_OBJECT ${CMAKE_CURRENT_BINARY_DIR}/nvme-polling.o)
astia_nvme_add_variant(nvme_msix_module
        ${ASTIA_NVME_MSIX_OBJECT} FALSE)
astia_nvme_add_variant(nvme_polling_module
        ${ASTIA_NVME_POLLING_OBJECT} TRUE)
if(ASTIA_NVME_FORCE_POLLING)
    add_custom_target(nvme_module DEPENDS nvme_polling_module)
else()
    add_custom_target(nvme_module DEPENDS nvme_msix_module)
endif()

add_custom_target(nvme_host_tests
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-host.sh
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../..
        VERBATIM)
if(ASTIA_BUILD_TESTS)
    add_test(NAME nvme_protocol_unit
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-host.sh)
    astia_append_test_label("module;nvme;host;deterministic")
endif()

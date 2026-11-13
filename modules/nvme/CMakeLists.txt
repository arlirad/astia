set(ASTIA_NVME_OBJECT ${CMAKE_CURRENT_BINARY_DIR}/nvme.o)

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
                -mcmodel=large -fno-asynchronous-unwind-tables -Wall -Wextra -pedantic
                ${ASTIA_NVME_MODE}
                -I${ASTIA_KERNEL_SOURCE_DIR}/include
                -c ${CMAKE_CURRENT_SOURCE_DIR}/nvme.cpp -o ${ASTIA_NVME_OBJECT}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/nvme.cpp
                ${ASTIA_KERNEL_SOURCE_DIR}/include/astia/module_abi.h
        COMMENT "Compiling NVMe loadable module"
        VERBATIM
)

add_custom_target(nvme_module ALL DEPENDS ${ASTIA_NVME_OBJECT})

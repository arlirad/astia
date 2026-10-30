add_library(log STATIC
        log.cpp
)

target_include_directories(log PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${ASTIA_KERNEL_SOURCE_DIR}/include
        ${ASTIA_KERNEL_SOURCE_DIR}/src/dev
        ${ASTIA_KERNEL_SOURCE_DIR}/src/fs
        ${ASTIA_KERNEL_SOURCE_DIR}/src/mm
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc
        ${ASTIA_KERNEL_SOURCE_DIR}/src/lib
        ${ASTIA_KERNEL_SOURCE_DIR}/src/arch
        ${ASTIA_KERNEL_SOURCE_DIR}/src/arch/x86_64
)

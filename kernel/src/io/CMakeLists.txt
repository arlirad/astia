add_library(io STATIC
        aio/aio.cpp
        aio/core_handlers.cpp
        aio/dispatch.cpp
        aio/registry.cpp
        aio/ring.cpp
        readiness.cpp
        readiness_handlers.cpp
)

target_include_directories(io PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${ASTIA_KERNEL_SOURCE_DIR}/include
        ${ASTIA_KERNEL_SOURCE_DIR}/src
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc
        ${ASTIA_KERNEL_SOURCE_DIR}/src/sys
)

target_link_libraries(io PUBLIC libcore proc core arch_${ARCH} mm)

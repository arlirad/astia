file(GLOB ASTIA_IO_AIO_CORE_HANDLER_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/aio/core/*.cpp)
file(GLOB ASTIA_IO_READINESS_HANDLER_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/readiness/aio/*.cpp)

add_library(io STATIC
        aio/aio.cpp
        aio/dispatch.cpp
        aio/registry.cpp
        aio/runner.cpp
        aio/ring.cpp
        request_queue.cpp
        ${ASTIA_IO_AIO_CORE_HANDLER_SOURCES}
        ${ASTIA_IO_READINESS_HANDLER_SOURCES}
)

target_include_directories(io PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${ASTIA_KERNEL_SOURCE_DIR}/include
        ${ASTIA_KERNEL_SOURCE_DIR}/src
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc
        ${ASTIA_KERNEL_SOURCE_DIR}/src/sys
)

target_link_libraries(io PUBLIC libcore proc core arch_${ARCH} mm)

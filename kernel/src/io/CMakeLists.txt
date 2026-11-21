file(GLOB ASTIA_AIO_OPERATION_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/aio/operations/*.cpp)

add_library(io STATIC
        aio/aio.cpp
        aio/dispatch.cpp
        aio/filesystem.cpp
        aio/readiness.cpp
        aio/registry.cpp
        aio/socket.cpp
        aio/ring.cpp
        ${ASTIA_AIO_OPERATION_SOURCES}
)

target_include_directories(io PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${ASTIA_KERNEL_SOURCE_DIR}/include
        ${ASTIA_KERNEL_SOURCE_DIR}/src
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc
        ${ASTIA_KERNEL_SOURCE_DIR}/src/sys
)

target_link_libraries(io PUBLIC libcore proc core arch_${ARCH} mm)

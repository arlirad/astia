add_library(sys STATIC
        aio.cpp
        aio_dispatch.cpp
        aio_filesystem.cpp
        aio_readiness.cpp
        aio_socket.cpp
        aio_ring.cpp
        syscall.cpp
        time.cpp
        init_sys.cpp
)

target_include_directories(sys PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${ASTIA_KERNEL_SOURCE_DIR}/include
)

target_link_libraries(sys PUBLIC libcore proc core arch_${ARCH} fs)

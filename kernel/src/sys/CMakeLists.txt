add_library(sys STATIC
        aio.cpp
        syscall.cpp
        handler_registry.cpp
        time.cpp
        init_sys.cpp
)

target_include_directories(sys PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${ASTIA_KERNEL_SOURCE_DIR}/include
)

target_link_libraries(sys PUBLIC libcore proc core arch_${ARCH} fs io)

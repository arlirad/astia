file(GLOB_RECURSE ASTIA_NET_SYSCALL_SOURCES CONFIGURE_DEPENDS *syscalls*/*_handler.cpp)
add_library(net STATIC
        ${ASTIA_NET_SYSCALL_SOURCES} net.cpp)

target_include_directories(net PUBLIC
        ${ASTIA_KERNEL_SOURCE_DIR}/include
        ${ASTIA_KERNEL_SOURCE_DIR}/src/sys)
target_link_libraries(net PUBLIC libcore)

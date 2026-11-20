set(ASTIA_SYS_HANDLER_CPP
        syscalls/clock_getres.cpp
        syscalls/clock_gettime.cpp
)
file(GLOB_RECURSE ASTIA_SYS_SYSCALL_SOURCES CONFIGURE_DEPENDS syscalls/*.cpp)
add_library(sys STATIC
        ${ASTIA_SYS_SYSCALL_SOURCES}
        aio.cpp
        syscall.cpp
        handler_registry.cpp
        time.cpp
        init_sys.cpp

        ${ASTIA_SYS_HANDLER_CPP})

target_include_directories(sys PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${ASTIA_KERNEL_SOURCE_DIR}/include
)

target_link_libraries(sys PUBLIC libcore proc core arch_${ARCH} fs io)

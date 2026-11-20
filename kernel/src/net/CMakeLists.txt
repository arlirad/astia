set(ASTIA_NET_HANDLER_CPP
        syscalls/accept.cpp
        syscalls/bind.cpp
        syscalls/connect.cpp
        syscalls/getsockopt.cpp
        syscalls/listen.cpp
        syscalls/recv.cpp
        syscalls/recvfrom.cpp
        syscalls/send.cpp
        syscalls/sendto.cpp
        syscalls/shutdown.cpp
        syscalls/socket.cpp
        syscalls/socketpair.cpp
)
file(GLOB_RECURSE ASTIA_NET_SYSCALL_SOURCES CONFIGURE_DEPENDS syscalls/*.cpp)
add_library(net STATIC
        ${ASTIA_NET_SYSCALL_SOURCES} net.cpp
        ${ASTIA_NET_HANDLER_CPP})

target_include_directories(net PUBLIC
        ${ASTIA_KERNEL_SOURCE_DIR}/include
        ${ASTIA_KERNEL_SOURCE_DIR}/src/sys)
target_link_libraries(net PUBLIC libcore)

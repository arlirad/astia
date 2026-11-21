add_library(net STATIC
        aio_dispatch.cpp
        aio_handlers.cpp
        net.cpp)

target_include_directories(net PUBLIC ${ASTIA_KERNEL_SOURCE_DIR}/include)
target_link_libraries(net PUBLIC libcore)

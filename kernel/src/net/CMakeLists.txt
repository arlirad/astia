file(GLOB ASTIA_NET_AIO_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/aio/*.cpp)

add_library(net STATIC
        ${ASTIA_NET_AIO_SOURCES}
        net.cpp
        link_driver.cpp
        protocol.cpp)

target_include_directories(net PUBLIC
        ${ASTIA_KERNEL_SOURCE_DIR}/include
        ${ASTIA_KERNEL_SOURCE_DIR}/src/sys)
target_link_libraries(net PUBLIC libcore)

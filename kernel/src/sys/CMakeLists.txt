add_library(sys STATIC
        stub.cpp
        io.cpp
        syscall.cpp
        time.cpp
        init_sys.cpp
)

target_include_directories(sys PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src/include
)

target_link_libraries(sys PUBLIC libcore proc core arch_${ARCH} fs)

file(GLOB_RECURSE ASTIA_DL_SYSCALL_SOURCES CONFIGURE_DEPENDS syscalls/*.cpp)
add_library(dl STATIC
        ${ASTIA_DL_SYSCALL_SOURCES}
        dl.cpp
        init.cpp
)

target_include_directories(dl PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${ASTIA_KERNEL_SOURCE_DIR}/include
        ${ASTIA_KERNEL_SOURCE_DIR}/src
        ${ASTIA_KERNEL_SOURCE_DIR}/src/proc
        ${ASTIA_KERNEL_SOURCE_DIR}/src/sys
)

target_link_libraries(dl PUBLIC proc mm elf fs core sys)

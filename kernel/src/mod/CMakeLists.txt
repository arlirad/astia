file(GLOB_RECURSE ASTIA_MOD_SYSCALL_SOURCES CONFIGURE_DEPENDS syscalls/*.cpp)

add_library(mod STATIC
        ${ASTIA_MOD_SYSCALL_SOURCES}
        exports.cpp
        module_driver.cpp
        module.cpp
        module_image.cpp
        irq_route.cpp
        init.cpp
)

target_include_directories(mod PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${ASTIA_KERNEL_SOURCE_DIR}/include
        ${ASTIA_KERNEL_SOURCE_DIR}/src/log
        ${ASTIA_KERNEL_SOURCE_DIR}/src/sys
)

target_link_libraries(mod PUBLIC elf initrd libcore mm dev net log proc fs io sys)

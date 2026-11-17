add_library(mod STATIC
        exports.cpp
        module_driver.cpp
        module.cpp
        init_mod.cpp
)

target_include_directories(mod PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${ASTIA_KERNEL_SOURCE_DIR}/include
        ${ASTIA_KERNEL_SOURCE_DIR}/src/log
)

target_link_libraries(mod PUBLIC elf initrd libcore mm dev net log)

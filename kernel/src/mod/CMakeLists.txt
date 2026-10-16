add_library(mod STATIC
        exports.cpp
        module.cpp
        init_mod.cpp
)

target_include_directories(mod PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src/include
        ${CMAKE_SOURCE_DIR}/src/log/include
)

target_link_libraries(mod PUBLIC elf initrd libcore mm dev log)

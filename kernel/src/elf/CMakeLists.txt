add_library(elf STATIC
        elf.cpp
)

target_include_directories(elf PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src/include
)

target_link_libraries(elf PUBLIC libcore)

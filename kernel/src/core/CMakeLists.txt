add_library(core STATIC
        test.cpp
        trivial.cpp
        trivial.c
        init_core.cpp
)

target_include_directories(core PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src/include
)

target_link_libraries(core PUBLIC libcore mm util)

add_library(util STATIC
        stub.cpp
)

target_include_directories(util PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(util PUBLIC libcore)

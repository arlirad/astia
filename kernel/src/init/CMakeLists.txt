add_library(init STATIC
        init.cpp
)

target_include_directories(init PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src/include
        ${CMAKE_SOURCE_DIR}/src/log/include
)

# The orchestrator prints its ordering trace to the boot console.
target_link_libraries(init PUBLIC libcore dev fs log)

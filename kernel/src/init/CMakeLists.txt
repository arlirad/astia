add_library(init STATIC
        init.cpp
)

target_include_directories(init PUBLIC
        ${ASTIA_KERNEL_SOURCE_DIR}/include
        ${ASTIA_KERNEL_SOURCE_DIR}/src/log
)

# The orchestrator prints its ordering trace to the boot console.
target_link_libraries(init PUBLIC libcore dev fs log)

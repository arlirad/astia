add_library(core STATIC
        init_core.cpp
        fiber.cpp
        rcu_runtime.cpp
)

target_include_directories(core PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${ASTIA_KERNEL_SOURCE_DIR}/include
)

target_link_libraries(core PUBLIC libcore mm util)

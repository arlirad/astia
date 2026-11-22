set(_astia_core_sources
        coroutine_lifecycle.cpp
        init_core.cpp
        operation_registry.cpp
        resume_queue.cpp
        rcu_runtime.cpp
)

if (ASTIA_BUILD_EXPERIMENTAL_FIBER)
    list(APPEND _astia_core_sources fiber.cpp)
endif ()

add_library(core STATIC ${_astia_core_sources})

target_include_directories(core PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${ASTIA_KERNEL_SOURCE_DIR}/include
)

target_link_libraries(core PUBLIC libcore mm util)

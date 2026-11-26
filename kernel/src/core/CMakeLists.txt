set(_astia_core_sources
        init.cpp
        resume_queue.cpp
        rcu_runtime.cpp
        shutdown.cpp
)

add_library(core STATIC ${_astia_core_sources})

target_include_directories(core PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${ASTIA_KERNEL_SOURCE_DIR}/include
)

target_link_libraries(core PUBLIC libcore mm util log)

add_library(mm STATIC
        frame_allocator.cpp
        heap.cpp
        new.cpp
        phys_region.cpp
        pinned_buffer.cpp
        iovec.cpp
        init_mm.cpp
)

target_include_directories(mm PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
)

# mm is designed to be replaceable; keep minimal deps
target_link_libraries(mm PUBLIC libcore)

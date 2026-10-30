add_library(util INTERFACE)

target_include_directories(util INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${ASTIA_KERNEL_SOURCE_DIR}/include
)

target_link_libraries(util INTERFACE libcore)

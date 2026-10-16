add_library(log STATIC
        log.cpp
)

target_include_directories(log PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src/include
        ${CMAKE_SOURCE_DIR}/src/dev/include
        ${CMAKE_SOURCE_DIR}/src/fs/include
        ${CMAKE_SOURCE_DIR}/src/mm/include
        ${CMAKE_SOURCE_DIR}/src/proc/include
        ${CMAKE_SOURCE_DIR}/src/lib/include
        ${CMAKE_SOURCE_DIR}/src/arch/include
        ${CMAKE_SOURCE_DIR}/src/arch/x86_64/include
)

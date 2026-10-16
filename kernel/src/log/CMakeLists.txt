add_library(log STATIC
        log.cpp
)

target_include_directories(log PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src/dev
        ${CMAKE_SOURCE_DIR}/src/fs
        ${CMAKE_SOURCE_DIR}/src/mm
        ${CMAKE_SOURCE_DIR}/src/proc
        ${CMAKE_SOURCE_DIR}/src/lib
        ${CMAKE_SOURCE_DIR}/src/arch
        ${CMAKE_SOURCE_DIR}/src/arch/x86_64
)

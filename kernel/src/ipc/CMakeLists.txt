# IPC implementations are compiled by the subsystem libraries that own the
# surrounding interfaces; this target centralizes their include surface.
add_library(ipc INTERFACE)
target_include_directories(ipc INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${ASTIA_KERNEL_SOURCE_DIR}/include
)

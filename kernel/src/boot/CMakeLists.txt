# Boot selection
set(BOOT "virt" CACHE STRING "Target boot: virt, pc")
set_property(CACHE BOOT PROPERTY STRINGS virt pc)

add_subdirectory(${BOOT})

add_library(boot INTERFACE)
target_link_libraries(boot INTERFACE boot_${BOOT})

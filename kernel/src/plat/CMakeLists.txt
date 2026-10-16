# Platform selection - can extend to more platforms
set(PLATFORM "virt" CACHE STRING "Target platform: virt, pc")
set_property(CACHE PLATFORM PROPERTY STRINGS virt pc)

add_subdirectory(${PLATFORM})

# Backwards compatibility alias if needed
add_library(plat INTERFACE)
target_link_libraries(plat INTERFACE plat_${PLATFORM})

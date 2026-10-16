#pragma once

#include <astia/common.hpp>

// Host-side unit tests run in user mode, but frame_allocator and page_map
// treat physical addresses as plain pointers. To test them unchanged we back a
// low "RAM" window with real, mmap'd memory and point the kernel's physical
// addresses into it.
//
// The window address must match the __kernel_start / __kernel_end symbols the
// CMake target injects with -Wl,--defsym (see tests/CMakeLists.txt): the frame
// allocator parks its bitmap directly past __kernel_end, so the window has to
// cover it.
namespace astia_test {

    constexpr astia::u64 arena_base = 0x08000000ull; // 128 MiB
    constexpr astia::u64 arena_size = 8ull * 1024 * 1024;
    constexpr astia::u64 arena_end  = arena_base + arena_size;

    /// Size of the fake "kernel image"; the allocator reserves it and parks the
    /// bitmap (and thus the first allocatable frame) immediately after it.
    constexpr astia::u64 kernel_image_size = 0x2000;

    /// Map the arena once and return its base. Aborts if the fixed mapping
    /// cannot be placed.
    void* map_arena();

} // namespace astia_test

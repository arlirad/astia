#include <astia/log.hpp>

#include "../core/panic.hpp"
#include "heap_internal.hpp"

// The kernel's strong heap failure reporter. Keeping it out of libmm lets
// host tests link the allocator without the kernel log/panic stack; the weak
// trapping default in heap.cpp covers test-only builds.
namespace astia::mm::heap::detail {
    [[noreturn]] void report_fatal(const char* where, const char* reason, u64 block,
                                   u64 base, usz bytes) {
        log::root::print(log::level::panic,
                         "mm/heap: corrupt free list in %s: %s (block=0x%x base=0x%x "
                         "bytes=0x%x)",
                         where, reason, block, base, bytes);
        core::kpanic("mm/heap: corrupt free list in %s", where);
    }
} // namespace astia::mm::heap::detail

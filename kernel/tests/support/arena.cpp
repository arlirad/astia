#include "arena.hpp"

#include <cstdio>
#include <cstdlib>
#include <sys/mman.h>

namespace astia_test {

    auto map_arena() -> void* {
        static void* mapped = nullptr;
        if (mapped != nullptr) {
            return mapped;
        }

        void* wanted = reinterpret_cast<void*>(arena_base);
        // NOREPLACE rather than MAP_FIXED: never clobber an existing mapping,
        // and fail loudly if the window is unexpectedly occupied.
        void* got = ::mmap(wanted, arena_size, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE | MAP_NORESERVE, -1, 0);
        if (got != wanted) {
            std::fprintf(stderr, "astia_test: mmap of arena at 0x%llx failed (%p)\n",
                         static_cast<unsigned long long>(arena_base), got);
            std::abort();
        }

        mapped = got;
        return mapped;
    }

} // namespace astia_test

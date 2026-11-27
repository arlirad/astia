#include <astia/api/aio.h>
#include <astia/fs.hpp>
#include <astia/mm/heap.hpp>
#include <astia/mm/shptr.hpp>

#include "../src/proc/fd_table.hpp"
#include "support/test.hpp"

#include <cstdlib>
#include <new>
#include <thread>
#include <vector>

namespace astia::dev {
    mm::shptr<fs::open_file> open_console(u32) {
        return {};
    }
} // namespace astia::dev

namespace astia::mm::heap {
    optional<u8*> alloc(usize bytes) {
        if (bytes == 0)
            return error_t::einval;

        void* value = std::malloc(bytes);

        return value ? optional<u8*>{static_cast<u8*>(value)}
                     : optional<u8*>{error_t::enomem};
    }

    void free(void* ptr) {
        std::free(ptr);
    }
} // namespace astia::mm::heap

namespace astia::fs {
    open_file::~open_file() = default;
} // namespace astia::fs

namespace {
    using namespace astia;

    struct handle_storage {
        alignas(fs::open_file) unsigned char bytes[sizeof(fs::open_file)]{};
    };

    mm::shptr<fs::open_file> borrowed_handle(handle_storage& storage) {
        auto* file = ::new (static_cast<void*>(storage.bytes))
            fs::open_file(mm::shptr<fs::file>{}, 0u);

        return mm::shptr<fs::open_file>::borrowed(file);
    }
} // namespace

ASTIA_TEST(fd_table_allocate_returns_distinct_descriptors_under_competition) {
    using namespace astia;

    proc::fd_table table;
    handle_storage storage;
    const auto     handle = borrowed_handle(storage);

    constexpr i32 thread_count = 4;
    constexpr i32 per_thread   = 8;

    std::vector<std::vector<optional<fd_t>>> results(thread_count);
    std::vector<std::thread>                 workers;

    for (i32 thread = 0; thread < thread_count; ++thread) {
        results[thread].resize(per_thread);
        workers.emplace_back([&table, &results, handle, thread] {
            for (i32 slot = 0; slot < per_thread; ++slot) {
                results[thread][slot] = table.allocate(handle, 0, proc::max_process_fds);
            }
        });
    }

    for (auto& worker : workers) {
        worker.join();
    }

    bool seen[proc::max_process_fds]{};

    for (const auto& thread_results : results) {
        for (const auto& installed : thread_results) {
            ASTIA_CHECK(installed);

            if (!installed)
                continue;

            const i32 fd = installed.value.value;

            ASTIA_CHECK(fd >= 0 && fd < static_cast<i32>(proc::max_process_fds));

            if (fd < 0 || fd >= static_cast<i32>(proc::max_process_fds))
                continue;

            ASTIA_CHECK(!seen[fd]);

            seen[fd] = true;
        }
    }
}

ASTIA_TEST(fd_table_allocate_respects_maximum_and_rejects_empty_handle) {
    using namespace astia;

    proc::fd_table table;
    handle_storage storage;
    const auto     handle = borrowed_handle(storage);

    ASTIA_CHECK(!table.allocate({}, 0, 4));
    ASTIA_CHECK(!table.allocate(handle, 0, 0));

    for (u32 fd = 0; fd < 3; ++fd) {
        ASTIA_CHECK(table.allocate(handle, 0, 3));
    }

    ASTIA_CHECK(!table.allocate(handle, 0, 3));
}

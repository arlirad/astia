#include "arena.hpp"
#include "test.hpp"

#ifdef RENAME_NOREPLACE
#undef RENAME_NOREPLACE
#endif
#ifdef RENAME_EXCHANGE
#undef RENAME_EXCHANGE
#endif

#include <astia/api/termios.h>
#include <astia/mm/frame_allocator.hpp>
#include <astia/mm/heap.hpp>

#include "chr/tty/pty.hpp"

#include <cstdlib>

namespace astia::proc {
    mm::shptr<fs::open_file> controlling_terminal() {
        return {};
    }
} // namespace astia::proc

namespace {
    alignas(16) astia::u8 heap_storage[256 * 1024];

    bool no_mapping(astia::u64, astia::u64) {
        return true;
    }

    struct runtime_bootstrap {
        runtime_bootstrap() {
            astia_test::map_arena();

            astia::mm::memory_map map;

            map.add(astia_test::arena_base, astia_test::arena_size,
                    astia::mm::memory_type::usable);
            astia::mm::kernel_frames().init(map);
            astia::mm::heap::configure(heap_storage, sizeof heap_storage, no_mapping);

            if (!astia::mm::heap::init())
                std::abort();
        }
    } runtime_bootstrap_instance;
}

ASTIA_TEST(pty_node_index_accepts_valid_indices) {
    astia::u32 index = 0;

    ASTIA_CHECK(astia::dev::pty_node_index("/pts/0", &index));
    ASTIA_CHECK(index == 0);
    ASTIA_CHECK(astia::dev::pty_node_index("/pts/15", &index));
    ASTIA_CHECK(index == 15);
}

ASTIA_TEST(pty_node_index_rejects_invalid_paths) {
    astia::u32 index = 0;

    ASTIA_CHECK(!astia::dev::pty_node_index("/pts/", &index));
    ASTIA_CHECK(!astia::dev::pty_node_index("/pts/abc", &index));
    ASTIA_CHECK(!astia::dev::pty_node_index("/pts/-1", &index));
    ASTIA_CHECK(!astia::dev::pty_node_index("/pts/999999999999999999999", &index));
    ASTIA_CHECK(!astia::dev::pty_node_index("/pts/4294967296", &index));
    ASTIA_CHECK(!astia::dev::pty_node_index("/pts/4294967295", &index));
    ASTIA_CHECK(!astia::dev::pty_node_index("/pts/16", &index));
}

ASTIA_TEST(pty_index_overflow_cannot_alias_an_existing_slave) {
    auto master = astia::dev::pty_open_master(0);

    ASTIA_CHECK(master);

    astia::i32 unlock = 0;

    ASTIA_CHECK(master->ioctl(TIOCSPTLCK, &unlock, sizeof unlock) == 0);
    ASTIA_CHECK(astia::dev::pty_node_exists("/pts/0"));
    ASTIA_CHECK(!astia::dev::pty_node_exists("/pts/4294967296"));
    ASTIA_CHECK(!astia::dev::pty_node_exists("/pts/4294967307"));

    auto slave = astia::dev::pty_open_slave("/pts/0", 0);

    ASTIA_CHECK(slave);
    ASTIA_CHECK(!astia::dev::pty_open_slave("/pts/4294967296", 0));
    ASTIA_CHECK(!astia::dev::pty_open_slave("/pts/4294967307", 0));

    slave  = {};
    master = {};
}

ASTIA_TEST(pty_master_readv_nonblock_empty_returns_eagain) {
    auto master = astia::dev::pty_open_master(ASTIA_AIO_O_NONBLOCK);

    ASTIA_CHECK(master);

    astia::i32 unlock = 0;

    ASTIA_CHECK(master->ioctl(TIOCSPTLCK, &unlock, sizeof unlock) == 0);

    auto slave = astia::dev::pty_open_slave("/pts/0", 0);

    ASTIA_CHECK(slave);

    astia::u8 byte = 0;

    astia::fs::buffer entry{&byte, 1};

    auto task = master->readv(astia::span<const astia::fs::buffer>(&entry, 1), 0);

    ASTIA_CHECK(task.resume());
    ASTIA_CHECK(task.done());
    ASTIA_CHECK(task.failed());
    ASTIA_CHECK(task.error() == astia::error_t::eagain);

    slave  = {};
    master = {};
}

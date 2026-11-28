#include <astia/api/aio.h>
#include <astia/fs.hpp>
#include <astia/mm/frame_allocator.hpp>
#include <astia/mm/heap.hpp>

#include "arena.hpp"
#include "support/aio_test_file.hpp"
#include "test.hpp"

namespace astia::proc {
    namespace {
        mm::shptr<fs::open_file> controlling_terminal_value;
    }

    mm::shptr<fs::open_file> controlling_terminal() {
        return controlling_terminal_value;
    }

    void controlling_terminal(mm::shptr<fs::open_file> value) {
        controlling_terminal_value = static_cast<mm::shptr<fs::open_file>&&>(value);
    }
} // namespace astia::proc

namespace {
    alignas(16) astia::u8 devfs_tty_heap_storage[256 * 1024];

    bool no_mapping(astia::u64, astia::u64) {
        return true;
    }

    void initialize_runtime() {
        static bool initialized = false;
        if (initialized)
            return;

        astia_test::map_arena();

        astia::mm::memory_map map;

        map.add(astia_test::arena_base, astia_test::arena_size,
                astia::mm::memory_type::usable);
        astia::mm::kernel_frames().init(map);
        astia::mm::heap::configure(devfs_tty_heap_storage, sizeof devfs_tty_heap_storage,
                                   no_mapping);
        ASTIA_CHECK(astia::mm::heap::init());
        ASTIA_CHECK(astia::fs::init());

        initialized = true;
    }

    void install_terminal(astia::u32 flags) {
        auto* terminal = new aio_test::fake_file(astia::fs::file_type::character);

        terminal->submit_result = 1;

        astia::proc::controlling_terminal(aio_test::make_open_file(terminal, flags));
    }

    void check_access_mode(astia::u32 requested, bool readable, bool writable) {
        auto handle = astia::fs::open("/dev/tty", requested);

        ASTIA_CHECK(handle);
        ASTIA_CHECK((handle->flags() & astia::fs::open_file_access_mask) == requested);

        char byte = 0;

        ASTIA_CHECK(handle->read(&byte, 1) == (readable ? 1 : -EBADF));
        ASTIA_CHECK(handle->write(&byte, 1) == (writable ? 1 : -EBADF));
    }
} // namespace

ASTIA_TEST(devfs_tty_open_uses_the_requested_access_mode) {
    initialize_runtime();
    install_terminal(ASTIA_AIO_O_RDWR);

    check_access_mode(ASTIA_AIO_O_RDONLY, true, false);
    check_access_mode(ASTIA_AIO_O_WRONLY, false, true);
    check_access_mode(ASTIA_AIO_O_RDWR, true, true);
}

ASTIA_TEST(devfs_tty_open_does_not_inherit_the_terminal_access_mode) {
    initialize_runtime();
    install_terminal(ASTIA_AIO_O_RDONLY);

    check_access_mode(ASTIA_AIO_O_WRONLY, false, true);
}

ASTIA_TEST(devfs_tty_without_a_controlling_terminal_is_unavailable) {
    initialize_runtime();
    astia::proc::controlling_terminal({});

    ASTIA_CHECK(!astia::fs::open("/dev/tty", ASTIA_AIO_O_RDWR));
}

#include <astia/fs.hpp>
#include <astia/mm/heap.hpp>

#include "../src/ipc/pipe.hpp"
#include "support/test.hpp"

#include <cstdlib>

namespace astia::mm::heap {
    optional<u8*> alloc(usz bytes) {
        if (bytes == 0)
            return error_t::einval;

        void* memory = std::malloc(bytes);
        if (memory == nullptr)
            return error_t::enomem;

        return static_cast<u8*>(memory);
    }

    void free(void* pointer) {
        std::free(pointer);
    }
} // namespace astia::mm::heap

namespace astia::fs {
    open_file::~open_file() = default;

    long open_file::reserve_append(u32, u64* offset) {
        if (offset == nullptr)
            return -EINVAL;

        *offset = 0;

        return 0;
    }
} // namespace astia::fs

namespace {
    using astia::fs::open_file;
    using astia::mm::shptr;

    constexpr astia::u32 pipe_capacity = 4096;

    struct endpoints {
        shptr<open_file> read;
        shptr<open_file> write;
    };

    astia::optional<endpoints> make_pipe() {
        auto created = astia::fs::create_pipe();
        if (!created)
            return created.error;

        endpoints result{created.value.first, created.value.second};

        return result;
    }

    template <typename T>
    astia::i64 run_now(astia::core::task<T>& task) {
        task.resume();

        if (!task.done() || task.failed())
            return -1;

        return static_cast<astia::i64>(*task.value());
    }

    astia::i64 fill(open_file& writer, astia::u8* buffer, astia::usz length) {
        auto task = writer.write(astia::span<const astia::u8>(buffer, length));

        return run_now(task);
    }

    astia::i64 drain(open_file& reader, astia::u8* buffer, astia::usz length) {
        auto task = reader.read(astia::span<astia::u8>(buffer, length));

        return run_now(task);
    }
} // namespace

ASTIA_TEST(pipe_blocking_small_write_parks_without_writing_partial_bytes) {
    auto endpoints = make_pipe();

    ASTIA_CHECK(endpoints);

    if (!endpoints)
        return;

    astia::u8 fill_data[pipe_capacity]{};

    for (astia::usz i = 0; i < sizeof fill_data; ++i) {
        fill_data[i] = 0x11;
    }

    ASTIA_CHECK(fill(*endpoints.value.write, fill_data, sizeof fill_data) ==
                pipe_capacity);

    astia::u8 drained[pipe_capacity]{};

    ASTIA_CHECK(drain(*endpoints.value.read, drained, 50) == 50);

    astia::u8 record[100]{};

    for (astia::usz i = 0; i < sizeof record; ++i) {
        record[i] = 0xab;
    }

    auto pending =
        endpoints.value.write->write(astia::span<const astia::u8>(record, sizeof record));

    ASTIA_CHECK(pending.resume());
    ASTIA_CHECK(!pending.done());

    ASTIA_CHECK(drain(*endpoints.value.read, drained, sizeof drained) ==
                pipe_capacity - 50);

    for (astia::usz i = 0; i < pipe_capacity - 50; ++i) {
        ASTIA_CHECK(drained[i] == 0x11);
    }

    endpoints.value.write->parent()->pending().service();

    ASTIA_CHECK(pending.done());
    ASTIA_CHECK(!pending.failed());
    ASTIA_CHECK(*pending.value() == static_cast<astia::i64>(sizeof record));

    ASTIA_CHECK(drain(*endpoints.value.read, drained, sizeof record) ==
                static_cast<astia::i64>(sizeof record));

    for (astia::usz i = 0; i < sizeof record; ++i) {
        ASTIA_CHECK(drained[i] == 0xab);
    }
}

ASTIA_TEST(pipe_blocking_write_larger_than_capacity_still_returns_partial) {
    auto endpoints = make_pipe();

    ASTIA_CHECK(endpoints);

    if (!endpoints)
        return;

    astia::u8 fill_data[pipe_capacity]{};

    for (astia::usz i = 0; i < sizeof fill_data; ++i) {
        fill_data[i] = 0x22;
    }

    ASTIA_CHECK(fill(*endpoints.value.write, fill_data, sizeof fill_data) ==
                pipe_capacity);

    astia::u8 drained[pipe_capacity]{};

    ASTIA_CHECK(drain(*endpoints.value.read, drained, 50) == 50);

    astia::u8 large[pipe_capacity + 50]{};

    for (astia::usz i = 0; i < sizeof large; ++i) {
        large[i] = 0x33;
    }

    ASTIA_CHECK(fill(*endpoints.value.write, large, sizeof large) == 50);
}

ASTIA_TEST(pipe_nonblocking_small_write_returns_available_space) {
    auto endpoints = make_pipe();

    ASTIA_CHECK(endpoints);

    if (!endpoints)
        return;

    endpoints.value.write->set_status_flags(
        static_cast<astia::u32>(astia::fs::open_file_status_flags::nonblock));

    astia::u8 fill_data[pipe_capacity]{};

    for (astia::usz i = 0; i < sizeof fill_data; ++i) {
        fill_data[i] = 0x44;
    }

    ASTIA_CHECK(fill(*endpoints.value.write, fill_data, sizeof fill_data) ==
                pipe_capacity);

    astia::u8 drained[pipe_capacity]{};

    ASTIA_CHECK(drain(*endpoints.value.read, drained, 50) == 50);

    astia::u8 record[100]{};

    for (astia::usz i = 0; i < sizeof record; ++i) {
        record[i] = 0x55;
    }

    ASTIA_CHECK(fill(*endpoints.value.write, record, sizeof record) == 50);
}

ASTIA_TEST(pipe_blocking_small_write_with_free_space_writes_all) {
    auto endpoints = make_pipe();

    ASTIA_CHECK(endpoints);

    if (!endpoints)
        return;

    astia::u8 record[100]{};

    for (astia::usz i = 0; i < sizeof record; ++i) {
        record[i] = 0x66;
    }

    ASTIA_CHECK(fill(*endpoints.value.write, record, sizeof record) ==
                static_cast<astia::i64>(sizeof record));

    astia::u8 drained[100]{};

    ASTIA_CHECK(drain(*endpoints.value.read, drained, sizeof drained) ==
                static_cast<astia::i64>(sizeof drained));

    for (astia::usz i = 0; i < sizeof drained; ++i) {
        ASTIA_CHECK(drained[i] == 0x66);
    }
}

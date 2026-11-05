#include "support/test.hpp"
#undef RENAME_NOREPLACE
#undef RENAME_EXCHANGE
#undef RENAME_WHITEOUT
#include <astia/fs_async.hpp>

namespace {
    enum class submit_mode { synchronous_zero, parked_zero };

    submit_mode submit_mode_state = submit_mode::synchronous_zero;
    astia::core::async_operation<astia::i64>* parked_completion = nullptr;
    astia::usize synchronous_submits = 0;

    astia::core::task<astia::i64> read_many(
        const astia::mm::shptr<astia::fs::open_file>& file, astia::span<astia::u8> buffer) {
        astia::i64 total = 0;
        for (astia::usize i = 0; i < buffer.size(); ++i) {
            auto result = co_await astia::fs::read(
                file, astia::span<astia::u8>(buffer.data() + i, 1), i);
            if (!result)
                co_return result.error;
            total += result.value;
        }
        co_return total;
    }
}

namespace astia::fs {

    open_file::~open_file() = default;

    long open_file::submit(io_request& request) {
        if (submit_mode_state == submit_mode::synchronous_zero) {
            ++synchronous_submits;
            request.complete(true);
            return 0;
        }
        parked_completion = request.completion();
        return 0;
    }

}

ASTIA_TEST(fs_async_returns_synchronous_zero_completion) {
    astia::fs::open_file file({}, astia::fs::open_file_status_flags::none);
    auto                 handle = astia::mm::shptr<astia::fs::open_file>::borrowed(&file);
    astia::u8             buffer[1]{};
    submit_mode_state = submit_mode::synchronous_zero;
    synchronous_submits = 0;

    auto task = astia::fs::read(handle, astia::span<astia::u8>(buffer, sizeof buffer), 0);
    ASTIA_CHECK(task.resume());
    ASTIA_CHECK(task.done());
    ASTIA_CHECK(!task.failed());
    ASTIA_CHECK(task.value() != nullptr && *task.value() == 0);
}

ASTIA_TEST(fs_async_awaits_parked_zero_completion) {
    astia::fs::open_file file({}, astia::fs::open_file_status_flags::none);
    auto                 handle = astia::mm::shptr<astia::fs::open_file>::borrowed(&file);
    astia::u8             buffer[1]{};
    submit_mode_state = submit_mode::parked_zero;
    parked_completion = nullptr;

    auto task = astia::fs::read(handle, astia::span<astia::u8>(buffer, sizeof buffer), 0);
    ASTIA_CHECK(task.resume());
    ASTIA_CHECK(!task.done());
    ASTIA_CHECK(parked_completion != nullptr);
    ASTIA_CHECK(parked_completion->complete(17));
    ASTIA_CHECK(task.done());
    ASTIA_CHECK(!task.failed());
    ASTIA_CHECK(task.value() != nullptr && *task.value() == 17);
}

ASTIA_TEST(fs_async_handles_long_synchronous_nested_reads) {
    astia::fs::open_file file({}, astia::fs::open_file_status_flags::none);
    auto                 handle = astia::mm::shptr<astia::fs::open_file>::borrowed(&file);
    astia::u8             buffer[65]{};
    submit_mode_state = submit_mode::synchronous_zero;
    synchronous_submits = 0;

    auto task = read_many(handle, astia::span<astia::u8>(buffer, sizeof buffer));
    ASTIA_CHECK(task.resume());
    ASTIA_CHECK(task.done());
    ASTIA_CHECK(!task.failed());
    ASTIA_CHECK(task.value() != nullptr && *task.value() == 0);
    ASTIA_CHECK(synchronous_submits == sizeof buffer);
}

#include "support/test.hpp"
#undef RENAME_NOREPLACE
#undef RENAME_EXCHANGE
#undef RENAME_WHITEOUT
#include <astia/api/errno.h>
#include <astia/fs.hpp>

namespace {
    enum class submit_mode {
        synchronous_zero,
        synchronous_short,
        synchronous_error,
        parked_zero,
    };

    submit_mode submit_mode_state = submit_mode::synchronous_zero;
    astia::core::async_operation<astia::i64>* parked_completion   = nullptr;
    astia::io::request*                       parked_request      = nullptr;
    astia::usize                              synchronous_submits = 0;
    astia::io::direction last_direction = astia::io::direction::input;

    astia::core::task<astia::i64>
    read_many(const astia::mm::shptr<astia::fs::open_file>& file,
              astia::span<astia::u8>                        buffer) {
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

    long open_file::submit(io::request& request) {
        last_direction = request.direction();
        if (submit_mode_state == submit_mode::synchronous_zero) {
            ++synchronous_submits;
            request.complete(true);
            return 0;
        }
        if (submit_mode_state == submit_mode::synchronous_short) {
            ++synchronous_submits;
            return 2;
        }
        if (submit_mode_state == submit_mode::synchronous_error)
            return -EIO;
        parked_completion = request.completion();
        parked_request    = &request;
        return 0;
    }

}

ASTIA_TEST(fs_async_returns_synchronous_zero_completion) {
    astia::fs::open_file file({}, astia::fs::open_file_status_flags::none);
    auto                 handle = astia::mm::shptr<astia::fs::open_file>::borrowed(&file);
    astia::u8            buffer[1]{};
    submit_mode_state   = submit_mode::synchronous_zero;
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
    astia::u8            buffer[1]{};
    submit_mode_state = submit_mode::parked_zero;
    parked_completion = nullptr;
    parked_request    = nullptr;

    auto task = astia::fs::read(handle, astia::span<astia::u8>(buffer, sizeof buffer), 0);
    ASTIA_CHECK(task.resume());
    ASTIA_CHECK(!task.done());
    ASTIA_CHECK(parked_completion != nullptr);
    ASTIA_CHECK(parked_request != nullptr);
    parked_request->acknowledge();
    ASTIA_CHECK(parked_completion->complete(17));
    ASTIA_CHECK(task.done());
    ASTIA_CHECK(!task.failed());
    ASTIA_CHECK(task.value() != nullptr && *task.value() == 17);
}

ASTIA_TEST(fs_async_write_uses_submit_park_and_wake) {
    astia::fs::open_file file({}, astia::fs::open_file_status_flags::none);
    auto                 handle = astia::mm::shptr<astia::fs::open_file>::borrowed(&file);
    const astia::u8      buffer[2]{1, 2};
    submit_mode_state = submit_mode::parked_zero;
    parked_completion = nullptr;
    parked_request    = nullptr;
    last_direction    = astia::io::direction::input;

    auto task =
        astia::fs::write(handle, astia::span<const astia::u8>(buffer, sizeof buffer), 0);
    ASTIA_CHECK(task.resume());
    ASTIA_CHECK(!task.done());
    ASTIA_CHECK(last_direction == astia::io::direction::output);
    ASTIA_CHECK(parked_completion != nullptr);
    ASTIA_CHECK(parked_request != nullptr);
    parked_request->acknowledge();
    ASTIA_CHECK(parked_completion->complete(2));
    ASTIA_CHECK(task.done());
    ASTIA_CHECK(!task.failed());
    ASTIA_CHECK(task.value() != nullptr && *task.value() == 2);
}

ASTIA_TEST(fs_async_propagates_short_reads_and_errors) {
    astia::fs::open_file file({}, astia::fs::open_file_status_flags::none);
    auto                 handle = astia::mm::shptr<astia::fs::open_file>::borrowed(&file);
    astia::u8            buffer[4]{};

    submit_mode_state = submit_mode::synchronous_short;
    auto short_task =
        astia::fs::read_exact(handle, astia::span<astia::u8>(buffer, sizeof buffer), 0);
    ASTIA_CHECK(short_task.resume());
    ASTIA_CHECK(short_task.done());
    ASTIA_CHECK(short_task.failed());
    ASTIA_CHECK(short_task.error() == astia::error_t::eio);

    submit_mode_state = submit_mode::synchronous_error;
    auto error_task =
        astia::fs::read(handle, astia::span<astia::u8>(buffer, sizeof buffer), 0);
    ASTIA_CHECK(error_task.resume());
    ASTIA_CHECK(error_task.done());
    ASTIA_CHECK(error_task.failed());
    ASTIA_CHECK(error_task.error() == astia::error_t::eio);
}

ASTIA_TEST(fs_async_handles_long_synchronous_nested_reads) {
    astia::fs::open_file file({}, astia::fs::open_file_status_flags::none);
    auto                 handle = astia::mm::shptr<astia::fs::open_file>::borrowed(&file);
    astia::u8            buffer[65]{};
    submit_mode_state   = submit_mode::synchronous_zero;
    synchronous_submits = 0;

    auto task = read_many(handle, astia::span<astia::u8>(buffer, sizeof buffer));
    ASTIA_CHECK(task.resume());
    ASTIA_CHECK(task.done());
    ASTIA_CHECK(!task.failed());
    ASTIA_CHECK(task.value() != nullptr && *task.value() == 0);
    ASTIA_CHECK(synchronous_submits == sizeof buffer);
}

ASTIA_TEST(fs_async_cancels_parked_device_flight) {
    astia::fs::open_file file({}, astia::fs::open_file_status_flags::none);
    auto                 handle = astia::mm::shptr<astia::fs::open_file>::borrowed(&file);
    astia::u8            buffer[1]{};
    submit_mode_state = submit_mode::parked_zero;
    parked_completion = nullptr;
    parked_request    = nullptr;

    auto task = astia::fs::read(handle, astia::span<astia::u8>(buffer, sizeof buffer), 0);
    ASTIA_CHECK(task.resume());
    ASTIA_CHECK(!task.done());
    ASTIA_CHECK(parked_completion != nullptr);
    ASTIA_CHECK(parked_request != nullptr);
    auto* completion = parked_completion;
    auto* request    = parked_request;

    task.reset();
    ASTIA_CHECK(request->canceled());
    ASTIA_CHECK(!request->begin_device());
    ASTIA_CHECK(completion->complete(17));
    ASTIA_CHECK(!completion->complete(18));
    request->acknowledge();
}

ASTIA_TEST(fs_sync_rejects_missing_file) {
    astia::fs::open_file handle({}, astia::fs::open_file_status_flags::none);
    ASTIA_CHECK(handle.sync() == -EBADF);
}

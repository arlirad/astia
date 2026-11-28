#include "support/test.hpp"
#undef RENAME_NOREPLACE
#undef RENAME_EXCHANGE
#undef RENAME_WHITEOUT
#include <astia/api/errno.h>
#include <astia/fs.hpp>

#include <atomic>
#include <thread>

namespace {
    enum class submit_mode {
        synchronous_zero,
        synchronous_short,
        synchronous_error,
        synchronous_length,
        parked_zero,
        parked_completion_first,
    };

    submit_mode submit_mode_state = submit_mode::synchronous_zero;
    astia::core::completion<astia::i64>* parked_completion   = nullptr;
    astia::io::request*                  parked_request      = nullptr;
    astia::usz                           synchronous_submits = 0;
    astia::io::direction                 last_direction = astia::io::direction::input;

    astia::core::task<astia::i64>
    read_many(const astia::mm::shptr<astia::fs::open_file>& file,
              astia::span<astia::u8>                        buffer) {
        astia::i64 total = 0;

        for (astia::usz i = 0; i < buffer.size(); ++i) {
            auto result =
                co_await file->read(astia::span<astia::u8>(buffer.data() + i, 1), i);
            if (!result)
                co_return result.error;

            total += result.value;
        }

        co_return total;
    }
}

namespace astia::fs {
    open_file::~open_file() = default;

    long open_file::reserve_append(u32, u64* offset) {
        if (offset == nullptr)
            return -EINVAL;

        *offset = 0;

        return 0;
    }

    void open_file::commit_append(u64, long) {}

    class fake_node final : public file, public readable, public writable {
        public:
        optional<readable&> as_readable() override {
            return *this;
        }

        optional<writable&> as_writable() override {
            return *this;
        }

        core::task<i64> readv(span<const buffer> buffers, u64 offset,
                              core::cltoken token) override {
            return submit_readv(buffers, offset, token);
        }

        core::task<i64> writev(span<const buffer> buffers, u64 offset,
                               core::cltoken token, bool nonblock) override {
            return submit_writev(buffers, offset, token, nonblock);
        }

        long submit(io::request& request) override {
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
            if (submit_mode_state == submit_mode::synchronous_length) {
                ++synchronous_submits;

                return static_cast<long>(request.length());
            }
            parked_completion = request.completion();
            parked_request    = &request;
            return 0;
        }
    };
}

namespace {
    astia::mm::shptr<astia::fs::file> make_parent() {
        return astia::mm::shptr<astia::fs::file>(new astia::fs::fake_node());
    }
} // namespace

ASTIA_TEST(fs_async_returns_synchronous_zero_completion) {
    astia::fs::open_file file(make_parent(), astia::fs::open_file_status_flags::none);
    auto                 handle = astia::mm::shptr<astia::fs::open_file>::borrowed(&file);
    astia::u8            buffer[1]{};

    submit_mode_state   = submit_mode::synchronous_zero;
    synchronous_submits = 0;

    auto task = handle->read(astia::span<astia::u8>(buffer, sizeof buffer), 0);

    ASTIA_CHECK(task.resume());
    ASTIA_CHECK(task.done());
    ASTIA_CHECK(!task.failed());
    ASTIA_CHECK(task.value() != nullptr && *task.value() == 0);
}

ASTIA_TEST(fs_async_awaits_parked_zero_completion) {
    astia::fs::open_file file(make_parent(), astia::fs::open_file_status_flags::none);
    auto                 handle = astia::mm::shptr<astia::fs::open_file>::borrowed(&file);
    astia::u8            buffer[1]{};

    submit_mode_state = submit_mode::parked_zero;
    parked_completion = nullptr;
    parked_request    = nullptr;

    auto task = handle->read(astia::span<astia::u8>(buffer, sizeof buffer), 0);

    ASTIA_CHECK(task.resume());
    ASTIA_CHECK(!task.done());
    ASTIA_CHECK(parked_completion != nullptr);
    ASTIA_CHECK(parked_request != nullptr);

    if (parked_request != nullptr && parked_completion != nullptr) {
        parked_request->acknowledge();
        ASTIA_CHECK(parked_completion->complete(17));
    }

    ASTIA_CHECK(task.done());
    ASTIA_CHECK(!task.failed());
    ASTIA_CHECK(task.value() != nullptr && *task.value() == 17);
}

ASTIA_TEST(fs_async_write_uses_submit_park_and_wake) {
    astia::fs::open_file file(make_parent(), astia::fs::open_file_status_flags::none);
    auto                 handle = astia::mm::shptr<astia::fs::open_file>::borrowed(&file);
    const astia::u8      buffer[2]{1, 2};

    submit_mode_state = submit_mode::parked_zero;
    parked_completion = nullptr;
    parked_request    = nullptr;
    last_direction    = astia::io::direction::input;

    auto task = handle->write(astia::span<const astia::u8>(buffer, sizeof buffer), 0);

    ASTIA_CHECK(task.resume());
    ASTIA_CHECK(!task.done());
    ASTIA_CHECK(last_direction == astia::io::direction::output);
    ASTIA_CHECK(parked_completion != nullptr);
    ASTIA_CHECK(parked_request != nullptr);

    if (parked_request != nullptr && parked_completion != nullptr) {
        parked_request->acknowledge();
        ASTIA_CHECK(parked_completion->complete(2));
    }

    ASTIA_CHECK(task.done());
    ASTIA_CHECK(!task.failed());
    ASTIA_CHECK(task.value() != nullptr && *task.value() == 2);
}

ASTIA_TEST(fs_async_propagates_short_reads_and_errors) {
    astia::fs::open_file file(make_parent(), astia::fs::open_file_status_flags::none);
    auto                 handle = astia::mm::shptr<astia::fs::open_file>::borrowed(&file);
    astia::u8            buffer[4]{};

    submit_mode_state = submit_mode::synchronous_short;

    auto short_task =
        handle->read_exact(astia::span<astia::u8>(buffer, sizeof buffer), 0);

    ASTIA_CHECK(short_task.resume());
    ASTIA_CHECK(short_task.done());
    ASTIA_CHECK(short_task.failed());
    ASTIA_CHECK(short_task.error() == astia::error_t::eio);

    submit_mode_state = submit_mode::synchronous_error;

    auto error_task = handle->read(astia::span<astia::u8>(buffer, sizeof buffer), 0);

    ASTIA_CHECK(error_task.resume());
    ASTIA_CHECK(error_task.done());
    ASTIA_CHECK(error_task.failed());
    ASTIA_CHECK(error_task.error() == astia::error_t::eio);
}

ASTIA_TEST(fs_async_handles_long_synchronous_nested_reads) {
    astia::fs::open_file file(make_parent(), astia::fs::open_file_status_flags::none);
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
    astia::fs::open_file file(make_parent(), astia::fs::open_file_status_flags::none);
    auto                 handle = astia::mm::shptr<astia::fs::open_file>::borrowed(&file);
    astia::u8            buffer[1]{};

    submit_mode_state = submit_mode::parked_zero;
    parked_completion = nullptr;
    parked_request    = nullptr;

    auto task = handle->read(astia::span<astia::u8>(buffer, sizeof buffer), 0);

    ASTIA_CHECK(task.resume());
    ASTIA_CHECK(!task.done());
    ASTIA_CHECK(parked_completion != nullptr);
    ASTIA_CHECK(parked_request != nullptr);
    task.reset();

    if (parked_request != nullptr) {
        ASTIA_CHECK(parked_request->cancellation_requested());
        parked_request->acknowledge();
    }
}

ASTIA_TEST(fs_async_completion_first_keeps_parked_request_stable) {
    astia::fs::open_file file(make_parent(), astia::fs::open_file_status_flags::none);
    auto                 handle = astia::mm::shptr<astia::fs::open_file>::borrowed(&file);
    astia::u8            buffer[4]{};

    submit_mode_state = submit_mode::parked_completion_first;
    parked_completion = nullptr;
    parked_request    = nullptr;

    auto task = handle->read(astia::span<astia::u8>(buffer, sizeof buffer), 0);

    ASTIA_CHECK(task.resume());
    ASTIA_CHECK(!task.done());
    ASTIA_CHECK(parked_request != nullptr);
    ASTIA_CHECK(parked_completion != nullptr);

    if (parked_request != nullptr && parked_completion != nullptr) {
        ASTIA_CHECK(parked_request->begin_device());

        buffer[0] = 0xa5;

        ASTIA_CHECK(parked_completion->complete(4));
    }

    ASTIA_CHECK(task.done());

    if (parked_request != nullptr)
        parked_request->end_device();

    ASTIA_CHECK(!task.failed());
    ASTIA_CHECK(task.value() != nullptr && *task.value() == 4);
    ASTIA_CHECK(buffer[0] == 0xa5);
}

ASTIA_TEST(fs_async_active_cancel_releases_device_reference_once) {
    for (int cycle = 0; cycle < 64; ++cycle) {
        astia::fs::open_file file(make_parent(), astia::fs::open_file_status_flags::none);
        auto      handle = astia::mm::shptr<astia::fs::open_file>::borrowed(&file);
        astia::u8 buffer[1]{};

        submit_mode_state = submit_mode::parked_zero;
        parked_completion = nullptr;
        parked_request    = nullptr;

        auto task = handle->read(astia::span<astia::u8>(buffer, 1), 0);

        ASTIA_CHECK(task.resume());
        ASTIA_CHECK(parked_request != nullptr);

        auto* request = parked_request;
        if (request != nullptr) {
            ASTIA_CHECK(request->begin_device());
            task.reset();
            ASTIA_CHECK(request->cancellation_requested());

            if ((cycle & 1) != 0) {
                request->acknowledge();
                request->end_device();
            }
            else {
                request->end_device();
                request->acknowledge();
            }
        }
    }
}

ASTIA_TEST(fs_async_readv_walks_every_buffer) {
    astia::fs::open_file    file(make_parent(), astia::fs::open_file_status_flags::none);
    astia::u8               first[2]{};
    astia::u8               second[3]{};
    const astia::fs::buffer buffers[2]{{first, sizeof first}, {second, sizeof second}};

    submit_mode_state   = submit_mode::synchronous_length;
    synchronous_submits = 0;

    auto task = file.readv(astia::span<const astia::fs::buffer>(buffers, 2),
                           astia::fs::offset_current);

    ASTIA_CHECK(task.resume());
    ASTIA_CHECK(task.done());
    ASTIA_CHECK(!task.failed());
    ASTIA_CHECK(task.value() != nullptr && *task.value() == 5);
    ASTIA_CHECK(synchronous_submits == 2);
}

ASTIA_TEST(fs_async_writev_walks_every_buffer) {
    astia::fs::open_file    file(make_parent(), astia::fs::open_file_status_flags::none);
    astia::u8               first[4]{};
    astia::u8               second[1]{};
    const astia::fs::buffer buffers[2]{{first, sizeof first}, {second, sizeof second}};

    submit_mode_state   = submit_mode::synchronous_length;
    synchronous_submits = 0;

    auto task = file.writev(astia::span<const astia::fs::buffer>(buffers, 2),
                            astia::fs::offset_current);

    ASTIA_CHECK(task.resume());
    ASTIA_CHECK(task.done());
    ASTIA_CHECK(!task.failed());
    ASTIA_CHECK(task.value() != nullptr && *task.value() == 5);
    ASTIA_CHECK(synchronous_submits == 2);
}

ASTIA_TEST(fs_async_refuses_a_canceled_token_before_submit) {
    astia::fs::open_file file(make_parent(), astia::fs::open_file_status_flags::none);
    auto                 handle = astia::mm::shptr<astia::fs::open_file>::borrowed(&file);
    astia::u8            buffer[1]{};

    submit_mode_state   = submit_mode::synchronous_zero;
    synchronous_submits = 0;

    astia::core::clsource source;

    ASTIA_CHECK(source.cancel(astia::core::cancel_reason::request));

    auto task =
        handle->read(astia::span<astia::u8>(buffer, sizeof buffer), 0, source.token());

    ASTIA_CHECK(task.resume());
    ASTIA_CHECK(task.done());
    ASTIA_CHECK(task.failed());
    ASTIA_CHECK(task.error() == astia::error_t::ecanceled);
    ASTIA_CHECK(synchronous_submits == 0);
}

ASTIA_TEST(fs_async_canceled_while_parked_discards_the_result) {
    astia::fs::open_file file(make_parent(), astia::fs::open_file_status_flags::none);
    auto                 handle = astia::mm::shptr<astia::fs::open_file>::borrowed(&file);
    astia::u8            buffer[1]{};

    submit_mode_state = submit_mode::parked_zero;
    parked_completion = nullptr;
    parked_request    = nullptr;

    astia::core::clsource source;
    auto                  task =
        handle->read(astia::span<astia::u8>(buffer, sizeof buffer), 0, source.token());

    ASTIA_CHECK(task.resume());
    ASTIA_CHECK(!task.done());

    if (parked_request == nullptr || parked_completion == nullptr) {
        ASTIA_CHECK(false);

        return;
    }

    ASTIA_CHECK(source.cancel(astia::core::cancel_reason::request));

    parked_request->acknowledge();
    ASTIA_CHECK(parked_completion->complete(17));
    ASTIA_CHECK(task.done());
    ASTIA_CHECK(task.failed());
    ASTIA_CHECK(task.error() == astia::error_t::ecanceled);
}

ASTIA_TEST(fs_async_reset_epoch_discards_a_parked_result) {
    astia::fs::open_file file(make_parent(), astia::fs::open_file_status_flags::none);
    auto                 handle = astia::mm::shptr<astia::fs::open_file>::borrowed(&file);
    astia::u8            buffer[1]{};

    submit_mode_state = submit_mode::parked_zero;
    parked_completion = nullptr;
    parked_request    = nullptr;

    astia::core::clsource source;
    auto                  task =
        handle->read(astia::span<astia::u8>(buffer, sizeof buffer), 0, source.token());

    ASTIA_CHECK(task.resume());
    ASTIA_CHECK(!task.done());

    if (parked_request == nullptr || parked_completion == nullptr) {
        ASTIA_CHECK(false);

        return;
    }

    ASTIA_CHECK(source.cancel(astia::core::cancel_reason::shutdown));
    source.reset();

    parked_request->acknowledge();
    ASTIA_CHECK(parked_completion->complete(17));
    ASTIA_CHECK(task.done());
    ASTIA_CHECK(task.failed());
    ASTIA_CHECK(task.error() == astia::error_t::ecanceled);
}

ASTIA_TEST(fs_async_cancel_end_device_race_is_linearized) {
    for (int cycle = 0; cycle < 128; ++cycle) {
        astia::u8            buffer[1]{};
        astia::io::operation operation(buffer, 1, 0, astia::io::direction::input);

        auto& request = operation.request();

        ASTIA_CHECK(request.park_device());
        ASTIA_CHECK(request.begin_device());

        std::atomic<bool> start{false};

        std::thread cancel([&] {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            request.cancel_device(false);
        });
        std::thread end([&] {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            request.end_device();
        });

        start.store(true, std::memory_order_release);
        cancel.join();
        end.join();
        request.acknowledge();
    }
}

ASTIA_TEST(fs_sync_rejects_missing_file) {
    astia::fs::open_file handle({}, astia::fs::open_file_status_flags::none);

    ASTIA_CHECK(handle.sync() == -EBADF);
}

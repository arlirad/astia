#include <astia/fs_async.hpp>

#include <astia/core/async_operation.hpp>

namespace astia::fs {

    namespace {
        core::task<i64> submit_async(const mm::shptr<open_file>& file, void* buffer,
                                     u32 length, u64 offset, io_direction direction) {
            if (!file)
                co_return error_t::ebadf;
            auto completion = mm::uptr<core::async_operation<i64>>(
                new core::async_operation<i64>());
            if (!completion)
                co_return error_t::enomem;
            core::async_operation<i64>* operation = completion.get();
            io_packet packet(file, buffer, length, offset, direction,
                             static_cast<mm::uptr<core::async_operation<i64>>&&>(completion));
            const long submitted = packet.submit();
            if (submitted < 0)
                co_return static_cast<error_t>(-submitted);
            if (submitted > 0)
                co_return static_cast<i64>(submitted);
            auto result = co_await *operation;
            if (!result)
                co_return result.error;
            co_return result.value;
        }
    } // namespace

    core::task<i64> async_read(const mm::shptr<open_file>& file, span<u8> buffer,
                               u64 offset) {
        return submit_async(file, buffer.data(), static_cast<u32>(buffer.size()), offset,
                            io_direction::input);
    }

    core::task<i64> async_write(const mm::shptr<open_file>& file, span<const u8> buffer,
                                u64 offset) {
        return submit_async(file, const_cast<u8*>(buffer.data()),
                            static_cast<u32>(buffer.size()), offset, io_direction::output);
    }

    core::task<i64> async_read_exact(const mm::shptr<open_file>& file, span<u8> buffer,
                                     u64 offset) {
        if (buffer.size() > static_cast<usize>(~u32{0}))
            co_return error_t::erange;
        auto result = co_await async_read(file, buffer, offset);
        if (!result)
            co_return result.error;
        if (result.value != static_cast<i64>(buffer.size()))
            co_return error_t::eio;
        co_return result.value;
    }

} // namespace astia::fs

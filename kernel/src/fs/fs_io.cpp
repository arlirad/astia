#include <astia/fs.hpp>
#include <astia/mapping.hpp>

namespace astia::fs {

    namespace {
        core::task<i64> submit_async(const mm::shptr<open_file>& file, void* buffer,
                                     u32 length, u64 offset, io::direction direction) {
            if (!file)
                co_return error_t::ebadf;
            auto completion =
                mm::uptr<core::async_operation<i64>>(new core::async_operation<i64>());
            if (!completion)
                co_return error_t::enomem;
            io_packet packet(
                file, buffer, length, offset, direction,
                static_cast<mm::uptr<core::async_operation<i64>>&&>(completion));
            if (!packet)
                co_return error_t::enomem;
            core::async_operation<i64>* operation = packet.completion();
            if (!packet.request().park_device())
                co_return error_t::eagain;
            const long submitted = packet.submit();
            if (submitted < 0) {
                packet.request().acknowledge();
                co_return error_from_errno(-submitted);
            }
            if (submitted > 0) {
                packet.request().acknowledge();
                co_return static_cast<i64>(submitted);
            }
            if (packet.request().complete()) {
                packet.request().acknowledge();
                co_return 0;
            }
            auto result = co_await *operation;
            if (!result)
                co_return result.error;
            co_return result.value;
        }
    }

    core::task<i64> submit(const mm::shptr<open_file>& file, void* buffer, u32 length,
                           u64 offset, io::direction direction) {
        return submit_async(file, buffer, length, offset, direction);
    }

    core::task<i64> read(const mm::shptr<open_file>& file, span<u8> buffer, u64 offset) {
        if (buffer.size() > static_cast<usize>(~u32{0}))
            return core::task<i64>(error_t::erange);
        return submit(file, buffer.data(), static_cast<u32>(buffer.size()), offset,
                      io::direction::input);
    }

    core::task<i64> write(const mm::shptr<open_file>& file, span<const u8> buffer,
                          u64 offset) {
        if (buffer.size() > static_cast<usize>(~u32{0}))
            return core::task<i64>(error_t::erange);
        return submit(file, const_cast<u8*>(buffer.data()),
                      static_cast<u32>(buffer.size()), offset, io::direction::output);
    }

    core::task<i64> read_exact(const mm::shptr<open_file>& file, span<u8> buffer,
                               u64 offset) {
        auto result = co_await read(file, buffer, offset);
        if (!result)
            co_return result.error;
        if (result.value != static_cast<i64>(buffer.size()))
            co_return error_t::eio;
        co_return result.value;
    }

} // namespace astia::fs

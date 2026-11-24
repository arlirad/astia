#include <astia/fs.hpp>
#include <astia/mapping.hpp>

namespace astia::fs {

    namespace {
        core::task<i64> submit_async(const mm::shptr<open_file>& file, void* buffer,
                                     u32 length, u64 offset, io::direction direction,
                                     core::cltoken token) {
            if (token.canceled())
                co_return error_t::ecanceled;
            if (!file)
                co_return error_t::ebadf;
            auto completion =
                mm::uptr<core::completion<i64>>(new core::completion<i64>());
            if (!completion)
                co_return error_t::enomem;
            io_packet packet(file, buffer, length, offset, direction,
                             static_cast<mm::uptr<core::completion<i64>>&&>(completion));
            if (!packet)
                co_return error_t::enomem;
            core::completion<i64>* pending_completion = packet.completion();
            if (!packet.request().park_device())
                co_return error_t::eagain;
            const long submitted = packet.submit();
            if (submitted < 0) {
                packet.request().acknowledge();
                co_return error_from_errno(-submitted);
            }
            if (submitted > 0) {
                packet.request().acknowledge();
                if (token.canceled())
                    co_return error_t::ecanceled;
                co_return static_cast<i64>(submitted);
            }
            if (packet.request().complete()) {
                packet.request().acknowledge();
                if (token.canceled())
                    co_return error_t::ecanceled;
                co_return 0;
            }
            auto result = co_await *pending_completion;
            // The completion means the backend has quiesced its use of the
            // buffer.  A completion-first backend may still leave the
            // request in its active state; acknowledge it before the packet
            // releases its owner reference so the request/frame lifetime
            // extends through the terminal device transition.
            packet.request().acknowledge();
            if (token.canceled())
                co_return error_t::ecanceled;
            if (!result)
                co_return result.error;
            co_return result.value;
        }
    }

    core::task<i64> submit(const mm::shptr<open_file>& file, void* buffer, u32 length,
                           u64 offset, io::direction direction, core::cltoken token) {
        return submit_async(file, buffer, length, offset, direction, token);
    }

    core::task<i64> read(const mm::shptr<open_file>& file, span<u8> buffer, u64 offset,
                         core::cltoken token) {
        if (buffer.size() > static_cast<usize>(~u32{0}))
            return core::task<i64>(error_t::erange);
        return submit(file, buffer.data(), static_cast<u32>(buffer.size()), offset,
                      io::direction::input, token);
    }

    core::task<i64> write(const mm::shptr<open_file>& file, span<const u8> buffer,
                          u64 offset, core::cltoken token) {
        if (buffer.size() > static_cast<usize>(~u32{0}))
            return core::task<i64>(error_t::erange);
        return submit(file, const_cast<u8*>(buffer.data()),
                      static_cast<u32>(buffer.size()), offset, io::direction::output,
                      token);
    }

    core::task<i64> read_exact(const mm::shptr<open_file>& file, span<u8> buffer,
                               u64 offset, core::cltoken token) {
        auto result = co_await read(file, buffer, offset, token);
        if (!result)
            co_return result.error;
        if (result.value != static_cast<i64>(buffer.size()))
            co_return error_t::eio;
        co_return result.value;
    }

} // namespace astia::fs

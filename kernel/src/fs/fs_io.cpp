#include <astia/fs.hpp>
#include <astia/mapping.hpp>

namespace astia::fs {

    namespace {
        /// Submit one concrete stretch to the node behind the open file.  The
        /// request offset is already resolved, so the open-file position is
        /// advanced once by readv()/writev() when the whole vector is done.
        core::task<i64> submit_segment(const mm::shptr<open_file>& handle,
                                       const buffer& entry, u64 offset,
                                       io::direction direction, core::cltoken token) {
            if (token.canceled())
                co_return error_t::ecanceled;
            auto completion =
                mm::uptr<core::completion<i64>>(new core::completion<i64>());
            if (!completion)
                co_return error_t::enomem;
            io_packet packet(handle, entry.data, static_cast<u32>(entry.length), offset,
                             direction,
                             static_cast<mm::uptr<core::completion<i64>>&&>(completion));
            if (!packet)
                co_return error_t::enomem;
            packet.request().updates_position(false);
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

        bool buffer_bytes(span<const buffer> buffers, u64& total) {
            total = 0;
            for (usize i = 0; i < buffers.size(); ++i) {
                const buffer& entry = buffers[i];
                if (entry.length > static_cast<usize>(~u32{0}))
                    return false;
                if (total > ~u64{0} - entry.length)
                    return false;
                total += entry.length;
            }
            return true;
        }
    } // namespace

    core::task<i64> open_file::readv(span<const buffer> buffers, u64 offset,
                                     core::cltoken token) {
        u64 sized = 0;
        if (!buffer_bytes(buffers, sized))
            co_return error_t::erange;
        if (sized == 0)
            co_return i64{0};
        const bool current = offset == offset_current;
        const u64  actual  = current ? current_position() : offset;
        i64        total   = 0;
        const auto self    = mm::shptr<open_file>::borrowed(this);
        for (usize i = 0; i < buffers.size(); ++i) {
            const buffer& entry = buffers[i];
            if (entry.length == 0)
                continue;
            auto result =
                co_await submit_segment(self, entry, actual + static_cast<u64>(total),
                                        io::direction::input, token);
            if (!result) {
                if (total != 0)
                    co_return total;
                co_return result.error;
            }
            total += result.value;
            if (result.value < static_cast<i64>(entry.length))
                break;
        }
        if (total > 0 && current)
            set_position(actual + static_cast<u64>(total));
        co_return total;
    }

    core::task<i64> open_file::writev(span<const buffer> buffers, u64 offset,
                                      core::cltoken token) {
        u64 sized = 0;
        if (!buffer_bytes(buffers, sized))
            co_return error_t::erange;
        if (sized == 0)
            co_return i64{0};
        const bool current = offset == offset_current;
        const bool append =
            (_flags & static_cast<u32>(open_file_status_flags::append)) != 0;
        u64 actual = current ? current_position() : offset;
        if (append) {
            const long reserved = reserve_append(static_cast<u32>(sized), &actual);
            if (reserved < 0)
                co_return error_from_errno(-reserved);
        }
        i64        total = 0;
        const auto self  = mm::shptr<open_file>::borrowed(this);
        for (usize i = 0; i < buffers.size(); ++i) {
            const buffer& entry = buffers[i];
            if (entry.length == 0)
                continue;
            auto result =
                co_await submit_segment(self, entry, actual + static_cast<u64>(total),
                                        io::direction::output, token);
            if (!result) {
                if (total != 0)
                    co_return total;
                co_return result.error;
            }
            total += result.value;
            if (result.value < static_cast<i64>(entry.length))
                break;
        }
        if (total > 0 && (current || append))
            set_position(actual + static_cast<u64>(total));
        co_return total;
    }

    core::task<i64> submit(const mm::shptr<open_file>& handle, void* buffer_ptr,
                           u32 length, u64 offset, io::direction direction,
                           core::cltoken token) {
        if (!handle)
            co_return error_t::ebadf;
        buffer entry{buffer_ptr, length};
        auto   result =
            direction == io::direction::input
                  ? co_await handle->readv(span<const buffer>(&entry, 1), offset, token)
                  : co_await handle->writev(span<const buffer>(&entry, 1), offset, token);
        if (!result)
            co_return result.error;
        co_return result.value;
    }

    core::task<i64> read(const mm::shptr<open_file>& handle, span<u8> buffer, u64 offset,
                         core::cltoken token) {
        if (buffer.size() > static_cast<usize>(~u32{0}))
            return core::task<i64>(error_t::erange);
        return submit(handle, buffer.data(), static_cast<u32>(buffer.size()), offset,
                      io::direction::input, token);
    }

    core::task<i64> write(const mm::shptr<open_file>& handle, span<const u8> buffer,
                          u64 offset, core::cltoken token) {
        if (buffer.size() > static_cast<usize>(~u32{0}))
            return core::task<i64>(error_t::erange);
        return submit(handle, const_cast<u8*>(buffer.data()),
                      static_cast<u32>(buffer.size()), offset, io::direction::output,
                      token);
    }

    core::task<i64> read_exact(const mm::shptr<open_file>& handle, span<u8> buffer,
                               u64 offset, core::cltoken token) {
        auto result = co_await read(handle, buffer, offset, token);
        if (!result)
            co_return result.error;
        if (result.value != static_cast<i64>(buffer.size()))
            co_return error_t::eio;
        co_return result.value;
    }

} // namespace astia::fs

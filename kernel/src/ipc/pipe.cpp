#include "pipe.hpp"

#include <astia/api/errno.h>
#include <astia/atomic.hpp>
#include <astia/core/spinlock.hpp>
#include <astia/mm/share.hpp>

#include "../sys/aio.hpp"

namespace astia::fs {
    namespace {
        constexpr u32 pipe_capacity_bytes = 4096;

        class pipe_file final : public file,
                                public readable,
                                public writable,
                                public pollable {
            public:
            pipe_file() : _readers(1), _writers(1) {}

            optional<readable&> as_readable() override {
                return *this;
            }

            optional<writable&> as_writable() override {
                return *this;
            }

            optional<pollable&> as_pollable() override {
                return *this;
            }

            file_mode mode() const override {
                return file_mode(file_type::fifo, default_file_permissions);
            }

            core::task<i64> readv(span<const buffer> buffers, u64 offset,
                                  core::cltoken token, bool nonblock) override {
                return submit_readv(buffers, offset, token, nonblock);
            }

            core::task<i64> writev(span<const buffer> buffers, u64 offset,
                                   core::cltoken token, bool nonblock) override {
                return submit_writev(buffers, offset, token, nonblock);
            }

            long read_ready(void* buffer, u32 length, u64) override {
                if (buffer == nullptr && length != 0)
                    return -EFAULT;
                u32 count = 0;
                {
                    core::spinlock_guard guard(_lock);

                    const u32 tail      = _tail;
                    const u32 head      = _head;
                    const u32 available = head - tail;
                    if (available == 0)
                        return 0;

                    count = available < length ? available : length;

                    auto* out = static_cast<u8*>(buffer);

                    for (u32 i = 0; i < count; ++i) {
                        out[i] = _data[(tail + i) & (pipe_capacity_bytes - 1)];
                    }

                    _tail = tail + count;
                }
                // A reader creates space for a parked writer.  Wake by
                // backing object rather than fd: fork/dup share the pipe,
                // while their descriptor numbers need not match.
                sys::io_device_ready_context(this);
                return count;
            }

            long write_ready(const void* buffer, u32 length, u64) override {
                return write_bytes(buffer, length, true);
            }

            u32 poll(u32 events, u32 flags) override {
                core::spinlock_guard guard(_lock);
                u32                  result = 0;
                if ((flags & pipe_read_endpoint_flag) != 0) {
                    if (_head != _tail)
                        result |= POLLIN;

                    if (astia::atomic_ref<u32>(_writers).load(
                            astia::memory_order::acquire) == 0)
                        result |= POLLIN | POLLHUP;
                }
                else if (astia::atomic_ref<u32>(_readers).load(
                             astia::memory_order::acquire) == 0) {
                    result |= POLLERR;
                }
                else if (pipe_capacity_bytes - (_head - _tail) != 0) {
                    result |= POLLOUT;
                }
                return result & (events | POLLERR | POLLHUP);
            }

            void close(u32 flags) override {
                u32* count =
                    (flags & pipe_read_endpoint_flag) != 0 ? &_readers : &_writers;
                if (astia::atomic_ref<u32>(*count).fetch_sub(
                        1u, astia::memory_order::acq_rel) == 1)
                    sys::io_device_ready_context(this);
            }

            void duplicate(u32 flags) override {
                u32* count =
                    (flags & pipe_read_endpoint_flag) != 0 ? &_readers : &_writers;
                astia::atomic_ref<u32>(*count).fetch_add(1u,
                                                         astia::memory_order::acq_rel);
            }

            long submit(io::request& request) override {
                const bool all_or_none = request.direction() == io::direction::output &&
                                         !request.nonblock() &&
                                         request.length() <= pipe_capacity_bytes;
                const long result =
                    request.direction() == io::direction::input
                        ? read_ready(request.buffer(), request.length(), 0)
                        : write_bytes(request.buffer(), request.length(), all_or_none);
                if (result == 0 && request.length() == 0)
                    request.complete(true);
                else if (result == 0 && request.direction() == io::direction::input &&
                         astia::atomic_ref<u32>(_writers).load(
                             astia::memory_order::acquire) == 0)
                    request.complete(true);
                else if (result == 0 && request.nonblock())
                    return -EAGAIN;
                else if (result == 0)
                    request.stage(1);
                return result;
            }

            private:
            /// Copy up to `length` bytes.  An all-or-none write (a blocking
            /// descriptor and a length no larger than PIPE_BUF) parks until
            /// the whole range fits, so two writers can never interleave
            /// partial records inside one atomic write.
            long write_bytes(const void* buffer, u32 length, bool all_or_none) {
                if (buffer == nullptr && length != 0)
                    return -EFAULT;

                if (astia::atomic_ref<u32>(_readers).load(astia::memory_order::acquire) ==
                    0)
                    return -EPIPE;

                u32 count = 0;

                {
                    core::spinlock_guard guard(_lock);

                    const u32 head = _head;
                    const u32 tail = _tail;
                    const u32 free = pipe_capacity_bytes - (head - tail);

                    if (free == 0 || (all_or_none && free < length))
                        return 0;

                    count = free < length ? free : length;

                    const auto* in = static_cast<const u8*>(buffer);

                    for (u32 i = 0; i < count; ++i) {
                        _data[(head + i) & (pipe_capacity_bytes - 1)] = in[i];
                    }

                    _head = head + count;
                }

                // A writer makes data available to parked readers.  The
                // engine retries the operation after the release above, so
                // its acquire load observes the published bytes.
                sys::io_device_ready_context(this);

                return count;
            }

            u32            _head = 0;
            u32            _tail = 0;
            core::spinlock _lock;
            u8             _data[pipe_capacity_bytes]{};
            u32            _readers;
            u32            _writers;
        };
    }

    optional<pipe_endpoints> create_pipe() {
        mm::shptr<pipe_file> file(new pipe_file{});

        if (!file)
            return error_t::enomem;

        auto read_end  = mm::share<open_file>(file, pipe_read_endpoint_flag);
        auto write_end = mm::share<open_file>(file, pipe_write_endpoint_flag);

        if (read_end && write_end)
            return pipe_endpoints{static_cast<mm::shptr<open_file>&&>(read_end),
                                  static_cast<mm::shptr<open_file>&&>(write_end)};

        return error_t::enomem;
    }
}

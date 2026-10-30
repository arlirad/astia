#include "pipe.hpp"

#include "../sys/aio.hpp"
#include <astia/api/errno.h>
#include <astia/core/spinlock.hpp>
#include <astia/mm/share.hpp>

namespace astia::fs {
    namespace {
        constexpr u32 capacity = 4096;
        constexpr u32 read_end  = 1u << 31;
        constexpr u32 write_end = 1u << 30;

        class pipe_file final : public file {
            public:
            pipe_file() : readers_(1), writers_(1) {}

            long read(void* buffer, u32 length, u64) override {
                if (buffer == nullptr && length != 0)
                    return -EFAULT;
                u32 count = 0;
                {
                    core::spinlock_guard guard(lock_);
                    const u32 tail      = tail_;
                    const u32 head      = head_;
                    const u32 available = head - tail;
                    if (available == 0)
                        return 0;
                    count = available < length ? available : length;
                    auto* out = static_cast<u8*>(buffer);
                    for (u32 i = 0; i < count; ++i)
                        out[i] = data_[(tail + i) & (capacity - 1)];
                    tail_ = tail + count;
                }
                // A reader creates space for a parked writer.  Wake by
                // backing object rather than fd: fork/dup share the pipe,
                // while their descriptor numbers need not match.
                sys::io_device_ready_context(this);
                return count;
            }

            long write(const void* buffer, u32 length, u64) override {
                if (buffer == nullptr && length != 0)
                    return -EFAULT;
                if (__atomic_load_n(&readers_, __ATOMIC_ACQUIRE) == 0)
                    return -EPIPE;
                u32 count = 0;
                {
                    core::spinlock_guard guard(lock_);
                    const u32 head = head_;
                    const u32 tail = tail_;
                    const u32 free = capacity - (head - tail);
                    if (free == 0)
                        return 0;
                    count = free < length ? free : length;
                    const auto* in = static_cast<const u8*>(buffer);
                    for (u32 i = 0; i < count; ++i)
                        data_[(head + i) & (capacity - 1)] = in[i];
                    head_ = head + count;
                }
                // A writer makes data available to parked readers.  The
                // engine retries the operation after the release above, so
                // its acquire load observes the published bytes.
                sys::io_device_ready_context(this);
                return count;
            }

            u32 poll(u32 events, u32 flags) override {
                core::spinlock_guard guard(lock_);
                u32 result = 0;
                if ((flags & read_end) != 0) {
                    if (head_ != tail_) result |= POLLIN;
                    if (__atomic_load_n(&writers_, __ATOMIC_ACQUIRE) == 0)
                        result |= POLLIN | POLLHUP;
                } else if (__atomic_load_n(&readers_, __ATOMIC_ACQUIRE) == 0) {
                    result |= POLLERR;
                } else if (capacity - (head_ - tail_) != 0) {
                    result |= POLLOUT;
                }
                return result & (events | POLLERR | POLLHUP);
            }

            void close(u32 flags) override {
                u32* count = (flags & read_end) != 0 ? &readers_ : &writers_;
                if (__atomic_fetch_sub(count, 1u, __ATOMIC_ACQ_REL) == 1)
                    sys::io_device_ready_context(this);
            }

            void duplicate(u32 flags) override {
                u32* count = (flags & read_end) != 0 ? &readers_ : &writers_;
                __atomic_fetch_add(count, 1u, __ATOMIC_ACQ_REL);
            }

            long submit(io_request& request) override {
                const long result = request.direction() == io_direction::input
                                        ? read(request.buffer(), request.length(), 0)
                                        : write(request.buffer(), request.length(), 0);
                if (result == 0 && request.length() == 0)
                    request.complete(true);
                else if (result == 0 && request.direction() == io_direction::input &&
                         __atomic_load_n(&writers_, __ATOMIC_ACQUIRE) == 0)
                    request.complete(true);
                else if (result == 0)
                    request.stage(1);
                return result;
            }

            const void* capability(file_capability kind) const override;

            private:
            u32 head_ = 0;
            u32 tail_ = 0;
            core::spinlock lock_;
            u8  data_[capacity]{};
            u32 readers_;
            u32 writers_;
        };

        long pipe_read(file* object, void* buffer, u32 length, u64 offset) {
            return static_cast<pipe_file*>(object)->read(buffer, length, offset);
        }
        long pipe_write(file* object, const void* buffer, u32 length, u64 offset) {
            return static_cast<pipe_file*>(object)->write(buffer, length, offset);
        }
        long pipe_submit(file* object, io_request& request) {
            return static_cast<pipe_file*>(object)->submit(request);
        }
        u32 pipe_poll(const file* object, u32 events, u32 flags) {
            return const_cast<pipe_file*>(static_cast<const pipe_file*>(object))->poll(events, flags);
        }

        const file_read_capability pipe_read_capability{pipe_read};
        const file_write_capability pipe_write_capability{pipe_write};
        const file_submit_capability pipe_submit_capability{pipe_submit};
        const file_poll_capability pipe_poll_capability{pipe_poll};

        const void* pipe_file::capability(file_capability kind) const {
            switch (kind) {
                case file_capability::read:   return &pipe_read_capability;
                case file_capability::write:  return &pipe_write_capability;
                case file_capability::submit: return &pipe_submit_capability;
                case file_capability::poll:   return &pipe_poll_capability;
                default:                      return file::capability(kind);
            }
        }
    }

    long create_pipe(mm::shptr<open_file>& read_end, mm::shptr<open_file>& write_end) {
        mm::shptr<pipe_file> object(new pipe_file{});
        if (!object)
            return -ENOMEM;
        read_end  = mm::share<open_file>(object, 1u << 31);
        write_end = mm::share<open_file>(object, 1u << 30);
        if (read_end && write_end)
            return 0;
        // Do not leave callers with a usable-looking half of a pipe after
        // an allocation failure.  In particular, the syscall layer cannot
        // install both descriptors atomically from a partial result.
        read_end  = {};
        write_end = {};
        return -ENOMEM;
    }
}

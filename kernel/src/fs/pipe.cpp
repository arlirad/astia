#include "pipe.hpp"

#include <astia/api/errno.h>
#include <astia/mm/share.hpp>

namespace astia::fs {
    namespace {
        constexpr u32 capacity = 4096;

        class pipe_file final : public file {
            public:
            long read(void* buffer, u32 length, u64) override {
                if (buffer == nullptr && length != 0) return -EFAULT;
                const u32 tail = __atomic_load_n(&tail_, __ATOMIC_RELAXED);
                const u32 head = __atomic_load_n(&head_, __ATOMIC_ACQUIRE);
                const u32 available = head - tail;
                if (available == 0) return 0;
                const u32 count = available < length ? available : length;
                auto* out = static_cast<u8*>(buffer);
                for (u32 i = 0; i < count; ++i)
                    out[i] = data_[(tail + i) & (capacity - 1)];
                __atomic_store_n(&tail_, tail + count, __ATOMIC_RELEASE);
                return count;
            }

            long write(const void* buffer, u32 length, u64) override {
                if (buffer == nullptr && length != 0) return -EFAULT;
                const u32 head = __atomic_load_n(&head_, __ATOMIC_RELAXED);
                const u32 tail = __atomic_load_n(&tail_, __ATOMIC_ACQUIRE);
                const u32 free = capacity - (head - tail);
                if (free == 0) return 0;
                const u32 count = free < length ? free : length;
                const auto* in = static_cast<const u8*>(buffer);
                for (u32 i = 0; i < count; ++i)
                    data_[(head + i) & (capacity - 1)] = in[i];
                __atomic_store_n(&head_, head + count, __ATOMIC_RELEASE);
                return count;
            }

            long submit(io_request& request) override {
                const long result = request.direction() == io_direction::input
                                         ? read(request.buffer(), request.length(), 0)
                                         : write(request.buffer(), request.length(), 0);
                if (result == 0 && request.length() == 0)
                    request.complete(true);
                else if (result == 0)
                    request.stage(1);
                return result;
            }

            private:
            u32 head_ = 0;
            u32 tail_ = 0;
            u8 data_[capacity]{};
        };
    }

    long create_pipe(mm::shptr<open_file>& read_end,
                     mm::shptr<open_file>& write_end) {
        mm::shptr<pipe_file> object(new pipe_file{});
        if (!object) return -ENOMEM;
        read_end = mm::share<open_file>(object, 0);
        write_end = mm::share<open_file>(object, 0);
        return read_end && write_end ? 0 : -ENOMEM;
    }
}

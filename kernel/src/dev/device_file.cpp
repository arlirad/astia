#include "device_file.hpp"

#include <astia/api/errno.h>
#include <astia/arch/cpu.hpp>
#include <astia/dev/blk/blkdev.hpp>

namespace astia::dev {

    void device_file::detach() {
        io::request* requests[max_pending_requests]{};
        u32          count = 0;
        {
            core::spinlock_guard guard(pending_lock_);
            __atomic_store_n(&detached_, true, __ATOMIC_RELEASE);
            count = pending_count_;
            for (u32 i = 0; i < count; ++i)
                requests[i] = pending_[i];
        }
        for (u32 i = 0; i < count; ++i)
            if (requests[i] != nullptr)
                requests[i]->cancel_device(true, core::cancel_reason::detach);
        for (;;) {
            bool pending = false;
            {
                core::spinlock_guard guard(pending_lock_);
                pending = pending_count_ != 0;
            }
            if (!pending && __atomic_load_n(&active_, __ATOMIC_ACQUIRE) == 0)
                break;
            ASTIA_CPU_RELAX();
        }
    }

    bool device_file::tracked_request(const io::request& request) const {
        core::spinlock_guard guard(pending_lock_);
        for (u32 i = 0; i < pending_count_; ++i)
            if (pending_[i] == &request)
                return true;
        return false;
    }

    bool device_file::track_request(io::request& request) const {
        core::spinlock_guard guard(pending_lock_);
        if (__atomic_load_n(&detached_, __ATOMIC_ACQUIRE))
            return false;
        for (u32 i = 0; i < pending_count_; ++i)
            if (pending_[i] == &request)
                return true;
        if (pending_count_ == max_pending_requests)
            return false;
        pending_[pending_count_++] = &request;
        request.cancellation_observer_set(const_cast<device_file*>(this));
        return true;
    }

    void device_file::untrack_request(io::request& request) const {
        core::spinlock_guard guard(pending_lock_);
        for (u32 i = 0; i < pending_count_; ++i) {
            if (pending_[i] != &request)
                continue;
            pending_[i]              = pending_[--pending_count_];
            pending_[pending_count_] = nullptr;
            request.cancellation_observer_set(nullptr);
            return;
        }
    }

    void device_file::cancellation_acknowledged(io::request& request) {
        untrack_request(request);
    }

    device* device_file::begin_operation(const io::request* request) const {
        __atomic_add_fetch(&active_, 1u, __ATOMIC_ACQ_REL);
        if (__atomic_load_n(&detached_, __ATOMIC_ACQUIRE) &&
            (request == nullptr || !tracked_request(*request))) {
            __atomic_sub_fetch(&active_, 1u, __ATOMIC_ACQ_REL);
            return nullptr;
        }
        device* value = device_instance();
        if (value == nullptr)
            __atomic_sub_fetch(&active_, 1u, __ATOMIC_ACQ_REL);
        return value;
    }

    void device_file::end_operation() const {
        __atomic_sub_fetch(&active_, 1u, __ATOMIC_ACQ_REL);
    }

    long device_file::read(void* buf, u32 len, u64 offset) {
        device* value = begin_operation();
        if (value == nullptr)
            return -ENODEV;
        const long result =
            value->read(astia::span<u8>(static_cast<u8*>(buf), len), offset);
        end_operation();
        return result;
    }

    long device_file::write(const void* buf, u32 len, u64 offset) {
        device* value = begin_operation();
        if (value == nullptr)
            return -ENODEV;
        const long result =
            value->write(astia::span<const u8>(static_cast<const u8*>(buf), len), offset);
        end_operation();
        return result;
    }

    long device_file::submit(io::request& request) {
        device* value = begin_operation(&request);
        if (value == nullptr)
            return -ENODEV;
        if (!track_request(request)) {
            end_operation();
            return __atomic_load_n(&detached_, __ATOMIC_ACQUIRE) ? -ENODEV : -EBUSY;
        }
        if (request.cancellation_requested() && !request.has_cancellation_handler()) {
            untrack_request(request);
            end_operation();
            return -ECANCELED;
        }
        const long result = value->submit(request);
        if (result == 0 && !request.complete() && !request.stable()) {
            request.cancel_device(false);
            untrack_request(request);
            end_operation();
            return -EAGAIN;
        }
        if (result != 0 || request.complete())
            untrack_request(request);
        end_operation();
        return result;
    }
    u32 device_file::poll(u32 events, u32 flags) {
        device* value = begin_operation();
        if (value == nullptr)
            return static_cast<u32>(POLLNVAL);
        const u32 result = value->poll(events, flags);
        end_operation();
        return result;
    }

    long device_file::ioctl(u64 request, void* argp, usize size) {
        device* value = begin_operation();
        if (value == nullptr)
            return -ENODEV;
        const long result =
            value->ioctl(request, astia::span<u8>(static_cast<u8*>(argp), size));
        end_operation();
        return result;
    }

    long device_file::acquire_controlling_terminal(i32 group) {
        device* value = begin_operation();
        if (value == nullptr)
            return -ENODEV;
        const long result = value->acquire_controlling_terminal(group);
        end_operation();
        return result;
    }

    long device_file::size() const {
        device* value = begin_operation();
        if (value == nullptr)
            return -ENODEV;
        const long result =
            (value->flags() & dv_block) != 0
                ? static_cast<long>(static_cast<const blk::blkdev*>(value)->size_bytes())
                : -ESPIPE;
        end_operation();
        return result;
    }

    u32 device_file::block_size() const {
        device* value = begin_operation();
        if (value == nullptr)
            return 0;
        const u32 result = (value->flags() & dv_block) != 0
                               ? static_cast<const blk::blkdev*>(value)->block_size()
                               : 0;
        end_operation();
        return result;
    }

} // namespace astia::dev

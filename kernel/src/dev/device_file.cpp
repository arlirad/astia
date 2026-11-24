#include "device_file.hpp"

#include <astia/api/errno.h>
#include <astia/arch/cpu.hpp>
#include <astia/dev/blk/blkdev.hpp>
#include <astia/mapping.hpp>

namespace astia::dev {

    bool device_file::quiescent() const {
        core::spinlock_guard guard(pending_lock_);
        return __atomic_load_n(&detached_, __ATOMIC_ACQUIRE) &&
               __atomic_load_n(&pending_count_, __ATOMIC_ACQUIRE) == 0 &&
               __atomic_load_n(&active_, __ATOMIC_ACQUIRE) == 0;
    }

    bool device_file::arm_quiescence_handler(quiescence_handler handler, void* context,
                                             u64 generation) {
        quiescence_handler callback         = nullptr;
        void*              callback_context = nullptr;
        {
            core::spinlock_guard guard(pending_lock_);
            quiescence_handler_    = handler;
            quiescence_context_    = context;
            quiescence_generation_ = generation;
            if (__atomic_load_n(&detached_, __ATOMIC_ACQUIRE) &&
                __atomic_load_n(&pending_count_, __ATOMIC_ACQUIRE) == 0 &&
                __atomic_load_n(&active_, __ATOMIC_ACQUIRE) == 0) {
                callback            = quiescence_handler_;
                callback_context    = quiescence_context_;
                quiescence_handler_ = nullptr;
                quiescence_context_ = nullptr;
            }
        }
        if (callback != nullptr)
            callback(callback_context, generation);
        return callback != nullptr;
    }

    void device_file::notify_quiescent() const {
        quiescence_handler callback   = nullptr;
        void*              context    = nullptr;
        u64                generation = 0;
        {
            core::spinlock_guard guard(pending_lock_);
            if (!__atomic_load_n(&detached_, __ATOMIC_ACQUIRE) ||
                __atomic_load_n(&pending_count_, __ATOMIC_ACQUIRE) != 0 ||
                __atomic_load_n(&active_, __ATOMIC_ACQUIRE) != 0)
                return;
            callback            = quiescence_handler_;
            context             = quiescence_context_;
            generation          = quiescence_generation_;
            quiescence_handler_ = nullptr;
            quiescence_context_ = nullptr;
        }
        if (callback != nullptr)
            callback(context, generation);
    }

    void device_file::detach() {
        io::request* requests[max_pending_requests]{};
        bool         retained[max_pending_requests]{};
        u32          count = 0;
        {
            core::spinlock_guard guard(pending_lock_);
            __atomic_store_n(&detached_, true, __ATOMIC_RELEASE);
            const u32 pending_count = __atomic_load_n(&pending_count_, __ATOMIC_ACQUIRE);
            for (u32 i = 0; i < pending_count; ++i) {
                if (!pending_owned_[i] || !pending_[i]->retain_for_device())
                    continue;
                requests[count] = pending_[i];
                retained[count] = true;
                ++count;
            }
        }
        for (u32 i = 0; i < count; ++i) {
            if (requests[i] != nullptr)
                requests[i]->cancel_device(true, core::cancel_reason::detach);
            if (retained[i])
                requests[i]->release_for_device();
        }
    }

    bool device_file::tracked_request(const io::request& request) const {
        core::spinlock_guard guard(pending_lock_);
        const u32            count = __atomic_load_n(&pending_count_, __ATOMIC_ACQUIRE);
        for (u32 i = 0; i < count; ++i)
            if (pending_[i] == &request)
                return true;
        return false;
    }

    bool device_file::track_request(io::request& request) const {
        core::spinlock_guard guard(pending_lock_);
        if (__atomic_load_n(&detached_, __ATOMIC_ACQUIRE))
            return false;
        const u32 count = __atomic_load_n(&pending_count_, __ATOMIC_ACQUIRE);
        for (u32 i = 0; i < count; ++i)
            if (pending_[i] == &request)
                return true;
        if (count == max_pending_requests)
            return false;
        const u32 index       = count;
        pending_[index]       = &request;
        pending_owned_[index] = request.retain_for_device();
        __atomic_store_n(&pending_count_, count + 1, __ATOMIC_RELEASE);
        request.cancellation_observer_set(const_cast<device_file*>(this));
        return true;
    }

    void device_file::untrack_request(io::request& request) const {
        {
            core::spinlock_guard guard(pending_lock_);
            const u32 count = __atomic_load_n(&pending_count_, __ATOMIC_ACQUIRE);
            for (u32 i = 0; i < count; ++i) {
                if (pending_[i] != &request)
                    continue;
                const u32  last      = count - 1;
                const bool owned     = pending_owned_[i];
                pending_[i]          = pending_[last];
                pending_owned_[i]    = pending_owned_[last];
                pending_[last]       = nullptr;
                pending_owned_[last] = false;
                __atomic_store_n(&pending_count_, last, __ATOMIC_RELEASE);
                request.cancellation_observer_set(nullptr);
                if (owned)
                    request.release_for_device();
                break;
            }
        }
        notify_quiescent();
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
        notify_quiescent();
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

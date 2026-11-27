#include "device_file.hpp"

#include <astia/api/errno.h>
#include <astia/arch/cpu.hpp>
#include <astia/atomic.hpp>
#include <astia/dev/blk/blkdev.hpp>
#include <astia/mapping.hpp>

namespace astia::dev {
    bool device_file::quiescent() const {
        core::spinlock_guard guard(pending_lock_);

        return astia::atomic_ref<bool>(detached_).load(astia::memory_order::acquire) &&
               astia::atomic_ref<u32>(pending_count_)
                       .load(astia::memory_order::acquire) == 0 &&
               astia::atomic_ref<u32>(active_).load(astia::memory_order::acquire) == 0;
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

            if (astia::atomic_ref<bool>(detached_).load(astia::memory_order::acquire) &&
                astia::atomic_ref<u32>(pending_count_)
                        .load(astia::memory_order::acquire) == 0 &&
                astia::atomic_ref<u32>(active_).load(astia::memory_order::acquire) == 0) {
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
        quiescence_handler callback = nullptr;
        void*              context  = nullptr;

        u64 generation = 0;

        {
            core::spinlock_guard guard(pending_lock_);

            if (!astia::atomic_ref<bool>(detached_).load(astia::memory_order::acquire) ||
                astia::atomic_ref<u32>(pending_count_)
                        .load(astia::memory_order::acquire) != 0 ||
                astia::atomic_ref<u32>(active_).load(astia::memory_order::acquire) != 0)
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

        bool retained[max_pending_requests]{};
        u32  count = 0;

        {
            core::spinlock_guard guard(pending_lock_);

            astia::atomic_ref<bool>(detached_).store(true, astia::memory_order::release);

            const u32 pending_count =
                astia::atomic_ref<u32>(pending_count_).load(astia::memory_order::acquire);

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

        const u32 count =
            astia::atomic_ref<u32>(pending_count_).load(astia::memory_order::acquire);

        for (u32 i = 0; i < count; ++i) {
            if (pending_[i] == &request)
                return true;
        }

        return false;
    }

    bool device_file::track_request(io::request& request) const {
        core::spinlock_guard guard(pending_lock_);

        if (astia::atomic_ref<bool>(detached_).load(astia::memory_order::acquire))
            return false;

        const u32 count =
            astia::atomic_ref<u32>(pending_count_).load(astia::memory_order::acquire);

        for (u32 i = 0; i < count; ++i) {
            if (pending_[i] == &request)
                return true;
        }

        if (count == max_pending_requests)
            return false;

        const u32 index = count;

        pending_[index]       = &request;
        pending_owned_[index] = request.retain_for_device();

        astia::atomic_ref<u32>(pending_count_)
            .store(count + 1, astia::memory_order::release);

        return true;
    }

    void device_file::untrack_request(io::request& request) const {
        {
            core::spinlock_guard guard(pending_lock_);

            const u32 count =
                astia::atomic_ref<u32>(pending_count_).load(astia::memory_order::acquire);

            for (u32 i = 0; i < count; ++i) {
                if (pending_[i] != &request)
                    continue;

                const u32  last  = count - 1;
                const bool owned = pending_owned_[i];

                pending_[i]          = pending_[last];
                pending_owned_[i]    = pending_owned_[last];
                pending_[last]       = nullptr;
                pending_owned_[last] = false;

                astia::atomic_ref<u32>(pending_count_)
                    .store(last, astia::memory_order::release);

                if (owned)
                    request.release_for_device();

                break;
            }
        }

        notify_quiescent();
    }

    device* device_file::begin_operation(const io::request* request) const {
        astia::atomic_ref<u32>(active_).add_fetch(1u, astia::memory_order::acq_rel);

        if (astia::atomic_ref<bool>(detached_).load(astia::memory_order::acquire) &&
            (request == nullptr || !tracked_request(*request))) {
            astia::atomic_ref<u32>(active_).sub_fetch(1u, astia::memory_order::acq_rel);

            return nullptr;
        }

        device* value = device_instance();
        if (value == nullptr)
            astia::atomic_ref<u32>(active_).sub_fetch(1u, astia::memory_order::acq_rel);

        return value;
    }

    void device_file::end_operation() const {
        astia::atomic_ref<u32>(active_).sub_fetch(1u, astia::memory_order::acq_rel);
        notify_quiescent();
    }

    core::task<i64> device_file::readv(span<const fs::buffer> buffers, u64 offset,
                                       core::cltoken token) {
        return submit_readv(buffers, offset, token);
    }

    core::task<i64> device_file::writev(span<const fs::buffer> buffers, u64 offset,
                                        core::cltoken token, bool nonblock) {
        return submit_writev(buffers, offset, token, nonblock);
    }

    long device_file::submit(io::request& request) {
        device* value = begin_operation(&request);
        if (value == nullptr)
            return -ENODEV;

        if (!track_request(request)) {
            end_operation();

            return astia::atomic_ref<bool>(detached_).load(astia::memory_order::acquire)
                       ? -ENODEV
                       : -EBUSY;
        }

        if (request.cancellation_requested()) {
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

    core::task<i64> device_file::ioctl(u64 request, void* argp, usz size, core::cltoken) {
        device* value = begin_operation();
        if (value == nullptr)
            co_return -ENODEV;

        const long result =
            value->ioctl(request, astia::span<u8>(static_cast<u8*>(argp), size));

        end_operation();

        co_return result;
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

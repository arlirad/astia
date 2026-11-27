#pragma once

#include <astia/api/errno.h>
#include <astia/arch/cpu.hpp>
#include <astia/arch/spinlock_guard.hpp>
#include <astia/atomic.hpp>
#include <astia/common.hpp>
#include <astia/core/mpsc_queue.hpp>
#include <astia/core/spinlock.hpp>
#include <astia/core/spsc_queue.hpp>

#include "tx_request.hpp"

namespace astia::net {
    template <u32 PendingDepth = 16, u32 CompletionDepth = 4>
    class tx_path final {
        struct completion {
            tx_request request{};
            int        status = 0;
        };

        core::bounded_mpsc_queue<tx_request, PendingDepth>    _pending;
        core::bounded_spsc_queue<completion, CompletionDepth> _completed;
        void (*_kick)(void*)                    = nullptr;
        void* _kick_context                     = nullptr;
        void (*_quiesce)(void*)                 = nullptr;
        void*                  _quiesce_context = nullptr;
        mutable core::spinlock _consumer{};
        mutable u64            _generation = 1;
        mutable u32            _accepting  = 1;
        u32                    _submitters = 0;
        u32                    _completers = 0;

        void reclaim(tx_request& request, int status) {
            if (request.reclaim != nullptr) {
                const auto callback = request.reclaim;

                request.reclaim = nullptr;

                callback(_context, &request, status);
            }
        }

        void stop_accepting() {
            atomic_ref<u32>(_accepting).store(0, memory_order::release);

            while (atomic_ref<u32>(_submitters).load(memory_order::acquire) != 0 ||
                   atomic_ref<u32>(_completers).load(memory_order::acquire) != 0) {
                arch::cpu::relax();
            }
        }

        void start_accepting() {
            atomic_ref<u32>(_accepting).store(1, memory_order::release);
        }

        public:
        explicit tx_path(void* context = nullptr) {
            initialize(context);
        }

        tx_path(const tx_path&)            = delete;
        tx_path& operator=(const tx_path&) = delete;

        void initialize(void* context = nullptr) {
            _pending.initialize();
            _completed.initialize();
            atomic_ref<u64>(_generation).store(1, memory_order::relaxed);
            atomic_ref<u32>(_accepting).store(1, memory_order::relaxed);
            atomic_ref<u32>(_submitters).store(0, memory_order::relaxed);
            atomic_ref<u32>(_completers).store(0, memory_order::relaxed);

            _context = context;
        }

        int submit(tx_request request) {
            if (request.data == nullptr || request.length == 0 ||
                request.reclaim == nullptr)
                return -EINVAL;

            if (atomic_ref<u32>(_accepting).load(memory_order::acquire) == 0)
                return -ECANCELED;

            atomic_ref<u32>(_submitters).add_fetch(1, memory_order::acquire);

            if (atomic_ref<u32>(_accepting).load(memory_order::acquire) == 0) {
                atomic_ref<u32>(_submitters).sub_fetch(1, memory_order::release);

                return -ECANCELED;
            }

            request.generation = atomic_ref<u64>(_generation).load(memory_order::acquire);

            const bool pushed = _pending.push(request);

            atomic_ref<u32>(_submitters).sub_fetch(1, memory_order::release);

            if (pushed && _kick != nullptr)
                _kick(_kick_context);

            return pushed ? 0 : -EAGAIN;
        }

        bool dequeue(tx_request& request) {
            arch::interrupt_spinlock_guard guard(_consumer);

            return _pending.pop(request);
        }

        bool complete(const tx_request& request, int status) {
            atomic_ref<u32>(_completers).add_fetch(1, memory_order::acquire);

            if (atomic_ref<u32>(_accepting).load(memory_order::acquire) == 0 ||
                request.generation !=
                    atomic_ref<u64>(_generation).load(memory_order::acquire)) {
                atomic_ref<u32>(_completers).sub_fetch(1, memory_order::release);

                return false;
            }

            const bool pushed = _completed.push({request, status});

            atomic_ref<u32>(_completers).sub_fetch(1, memory_order::release);

            return pushed;
        }

        bool cancel(const tx_request& request) {
            const u64 generation =
                atomic_ref<u64>(_generation).load(memory_order::acquire);

            if (request.generation != 0 && request.generation != generation)
                return false;

            stop_accepting();

            arch::interrupt_spinlock_guard guard(_consumer);

            tx_request pending{};

            bool found = false;

            core::bounded_spsc_queue<tx_request, PendingDepth> retained;

            while (_pending.pop(pending)) {
                if (!found && pending.cookie == request.cookie &&
                    pending.generation == generation) {
                    reclaim(pending, -ECANCELED);

                    found = true;
                }
                else {
                    retained.push(pending);
                }
            }

            while (retained.pop(pending)) {
                _pending.push(pending);
            }

            start_accepting();

            return found;
        }

        u32 poll() {
            arch::interrupt_spinlock_guard guard(_consumer);

            u32 count = 0;

            completion item{};

            while (_completed.pop(item)) {
                reclaim(item.request, item.status);
                ++count;
            }

            return count;
        }

        void reset() {
            stop_accepting();

            if (_quiesce != nullptr)
                _quiesce(_quiesce_context);

            arch::interrupt_spinlock_guard guard(_consumer);

            tx_request request{};

            while (_pending.pop(request)) {
                reclaim(request, -ECANCELED);
            }

            completion item{};

            while (_completed.pop(item)) {
                reclaim(item.request, -ECANCELED);
            }

            u64 generation = atomic_ref<u64>(_generation).load(memory_order::relaxed) + 1;
            if (generation == 0)
                generation = 1;

            atomic_ref<u64>(_generation).store(generation, memory_order::release);
            start_accepting();
        }

        bool abort_inflight(const tx_request& request, int status) {
            if (request.reclaim == nullptr)
                return false;

            tx_request copy = request;

            reclaim(copy, status);

            return true;
        }

        /// Called after a request is queued so the driver can program the
        /// device; runs in the submitter's context.
        void set_kick(void (*kick)(void*), void* context) {
            _kick         = kick;
            _kick_context = context;
        }

        /// Called by reset() before queued requests are reclaimed so the
        /// driver can stop hardware DMA and abort claimed descriptors.
        void set_quiesce(void (*quiesce)(void*), void* context) {
            _quiesce         = quiesce;
            _quiesce_context = context;
        }

        void set_generation(u64 value) {
            atomic_ref<u64>(_generation)
                .store(value == 0 ? 1 : value, memory_order::release);
        }

        u64 generation() const {
            return atomic_ref<u64>(_generation).load(memory_order::acquire);
        }

        private:
        void* _context = nullptr;
    };
} // namespace astia::net

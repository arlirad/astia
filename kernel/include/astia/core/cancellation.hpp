#pragma once

#include <astia/common.hpp>

namespace astia::core {

    enum class cancel_reason : u8 {
        none,
        request,
        owner,
        detach,
        timeout,
        shutdown,
        user                = request,
        owner_release       = owner,
        device_detach       = detach,
        controller_shutdown = shutdown,
    };

    using cancellation_reason = cancel_reason;

    class clsource;

    class cltoken {
        const clsource* _source     = nullptr;
        u64             _generation = 0;

        cltoken(const clsource* source, u64 generation)
            : _source(source), _generation(generation) {}

        friend class clsource;

        public:
        cltoken() = default;

        bool attached() const {
            return _source != nullptr;
        }
        u64 generation() const {
            return _generation;
        }
        bool stale() const;
        bool valid() const {
            return attached() && !stale();
        }
        bool cancellation_requested() const;
        bool requested() const {
            return cancellation_requested();
        }
        bool canceled() const {
            return cancellation_requested();
        }
        bool cancelled() const {
            return cancellation_requested();
        }
        cancel_reason reason() const;
    };

    class clsource {
        volatile u32 _state      = 0;
        volatile u64 _generation = 1;

        friend class cltoken;

        public:
        clsource()                           = default;
        clsource(const clsource&)            = delete;
        clsource& operator=(const clsource&) = delete;

        cltoken token() const {
            return cltoken(this, __atomic_load_n(&_generation, __ATOMIC_ACQUIRE));
        }

        u64 generation() const {
            return __atomic_load_n(&_generation, __ATOMIC_ACQUIRE);
        }

        bool cancel(cancel_reason reason = cancel_reason::request) {
            u32       expected = 0;
            const u32 value    = 1u | (static_cast<u32>(reason) << 1);
            return __atomic_compare_exchange_n(&_state, &expected, value, false,
                                               __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
        }

        bool request(cancel_reason reason = cancel_reason::request) {
            return cancel(reason);
        }

        void reset() {
            __atomic_store_n(&_state, 0, __ATOMIC_RELEASE);
            u64 generation = __atomic_load_n(&_generation, __ATOMIC_RELAXED);
            generation     = generation == ~u64{0} ? 1 : generation + 1;
            __atomic_store_n(&_generation, generation, __ATOMIC_RELEASE);
        }

        private:
        bool current(u64 generation) const {
            return generation != 0 &&
                   __atomic_load_n(&_generation, __ATOMIC_ACQUIRE) == generation;
        }

        bool requested(u64 generation) const {
            return current(generation) &&
                   (__atomic_load_n(&_state, __ATOMIC_ACQUIRE) & 1u) != 0;
        }

        cancel_reason reason(u64 generation) const {
            return current(generation)
                       ? static_cast<cancel_reason>(
                             __atomic_load_n(&_state, __ATOMIC_ACQUIRE) >> 1)
                       : cancel_reason::none;
        }
    };

    inline bool cltoken::stale() const {
        return _source == nullptr || !_source->current(_generation);
    }

    inline bool cltoken::cancellation_requested() const {
        return _source != nullptr && _source->requested(_generation);
    }

    inline cancel_reason cltoken::reason() const {
        return _source == nullptr ? cancel_reason::none : _source->reason(_generation);
    }

} // namespace astia::core

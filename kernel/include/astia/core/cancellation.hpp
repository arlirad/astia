#pragma once

#include <astia/atomic.hpp>
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

    /// A read-only cancellation stamp.  It observes its own source and every
    /// ancestor, so cancelling a process-wide source cancels all operation
    /// tokens created beneath it.  The nearest cancelled source supplies the
    /// reason.
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
        const clsource* source() const {
            return _source;
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

    /// One cancellation source.  A source created from another becomes its
    /// child; cancellation is observed lazily by walking to the root, so a
    /// child created after the parent was cancelled sees it immediately and
    /// no child list has to be maintained.
    ///
    /// A parent must outlive its children.  Resetting a source bumps its
    /// generation, which makes tokens of every descendant stale.
    class clsource {
        atomic<u32>     _state{0};
        atomic<u64>     _generation{1};
        const clsource* _parent            = nullptr;
        u64             _parent_generation = 0;

        friend class cltoken;

        public:
        clsource() = default;
        explicit clsource(clsource& parent)
            : _parent(&parent), _parent_generation(parent.generation()) {}
        clsource(const clsource&)            = delete;
        clsource& operator=(const clsource&) = delete;

        cltoken token() const {
            return cltoken(this, generation());
        }

        u64 generation() const {
            return _generation.load(memory_order::acquire);
        }

        const clsource* parent() const {
            return _parent;
        }

        bool cancel(cancel_reason reason = cancel_reason::request) {
            u32       expected = 0;
            const u32 value    = 1u | (static_cast<u32>(reason) << 1);
            return _state.compare_exchange_strong(expected, value);
        }

        bool request(cancel_reason reason = cancel_reason::request) {
            return cancel(reason);
        }

        void reset() {
            _state.store(0, memory_order::release);
            u64 generation = _generation.load(memory_order::relaxed);
            generation     = generation == ~u64{0} ? 1 : generation + 1;
            _generation.store(generation, memory_order::release);
        }

        private:
        bool _current(u64 generation) const {
            return generation != 0 && this->generation() == generation;
        }

        /// True when no ancestor has been reset since this source was created.
        bool _chain_current() const {
            const clsource* child = this;
            while (child->_parent != nullptr) {
                if (child->_parent->generation() != child->_parent_generation)
                    return false;
                child = child->_parent;
            }
            return true;
        }

        bool _requested() const {
            return (_state.load(memory_order::acquire) & 1u) != 0;
        }

        cancel_reason _reason() const {
            return static_cast<cancel_reason>(_state.load(memory_order::acquire) >> 1);
        }
    };

    inline bool cltoken::stale() const {
        return _source == nullptr || !_source->_current(_generation) ||
               !_source->_chain_current();
    }

    inline bool cltoken::cancellation_requested() const {
        if (stale())
            return false;
        for (const clsource* source = _source; source != nullptr;
             source                 = source->_parent) {
            if (source->_requested())
                return true;
        }
        return false;
    }

    inline cancel_reason cltoken::reason() const {
        if (stale())
            return cancel_reason::none;
        for (const clsource* source = _source; source != nullptr;
             source                 = source->_parent) {
            if (source->_requested())
                return source->_reason();
        }
        return cancel_reason::none;
    }

} // namespace astia::core

#pragma once

#include <astia/common.hpp>
#include <astia/span.hpp>

namespace astia::net {
    /// Builds a packet from the back: the payload is placed first, then each
    /// layer reserves space in front of it for its own header.  This keeps
    /// header layout with the layer that owns it.
    class packet_builder {
        public:
        packet_builder(u8* storage, usz capacity)
            : _storage(storage), _capacity(capacity), _offset(capacity) {}

        /// Reserves `length` bytes in front of the current data.  Returns the
        /// new start, or nullptr when the buffer is exhausted.
        u8* prepend(usz length) {
            if (_storage == nullptr || length > _offset)
                return nullptr;

            _offset -= length;

            return _storage + _offset;
        }

        span<const u8> data() const {
            return span<const u8>(_storage + _offset, length());
        }

        usz length() const {
            return _capacity - _offset;
        }

        private:
        u8* _storage  = nullptr;
        usz _capacity = 0;
        usz _offset   = 0;
    };
} // namespace astia::net

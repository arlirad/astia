#pragma once

#include <astia/common.hpp>
#include <astia/span.hpp>

namespace astia::net {
    /// A validated payload, possibly split across two buffers.  `owner` and
    /// `recycle` belong to the producer; sub-views clear them.
    struct packet_view {
        span<const u8> first{};
        span<const u8> second{};
        u32            length                    = 0;
        u32            wire_length               = 0;
        void*          owner                     = nullptr;
        void (*recycle)(void* owner, int status) = nullptr;

        /// Zero-copy sub-view of `length` bytes at `offset`.  The result is
        /// borrowed and clears recycle/owner; an out-of-range request yields
        /// an empty view.
        packet_view slice(u32 offset, u32 length) const {
            packet_view result{};

            const usz available = first.size() + second.size();

            if (offset > available || length > available - offset)
                return result;

            result         = *this;
            result.recycle = nullptr;
            result.owner   = nullptr;

            if (!result.advance(offset))
                return {};

            result.length = length;

            if (result.first.size() > length) {
                result.first  = span<const u8>(result.first.data(), length);
                result.second = {};
            }
            else {
                const u32 remaining = length - static_cast<u32>(result.first.size());

                if (result.second.size() > remaining)
                    result.second = span<const u8>(result.second.data(), remaining);
            }

            return result;
        }

        /// Typed convenience: copies sizeof(T) bytes at `offset` into a T.
        /// Returns a zero-initialized T when the view is too short, so callers
        /// must validate the length first (headers are fixed by contract).
        template <typename T>
        T slice(u32 offset = 0) const {
            T value{};

            (void) peek(offset, reinterpret_cast<u8*>(&value), sizeof(T));

            return value;
        }

        /// Copies `length` bytes at `offset`, leaving the view untouched.
        bool peek(u32 offset, u8* out, u32 length) const {
            const packet_view view = slice(offset, length);

            if (view.length != length)
                return false;

            return view.copy_to(out, length);
        }

        /// Copies `bytes` from the front into `out`, across segments.  Does
        /// not consume the view; call advance() for that.
        bool copy_to(u8* out, u32 bytes) const {
            if (out == nullptr)
                return false;

            const span<const u8> segments[2] = {first, second};

            u32 copied = 0;

            for (u32 segment_index = 0; segment_index < 2; ++segment_index) {
                const span<const u8>& segment = segments[segment_index];

                u32 take = static_cast<u32>(segment.size());

                if (take > bytes - copied)
                    take = bytes - copied;

                for (u32 index = 0; index < take; ++index) {
                    out[copied + index] = segment.data()[index];
                }

                copied += take;
                if (copied == bytes)
                    return true;
            }

            return false;
        }

        /// Consume `bytes` from the front.  `wire_length` keeps the original
        /// on-wire length for diagnostics.
        bool advance(u32 bytes) {
            const usz available = first.size() + second.size();

            if (bytes > available)
                return false;

            if (bytes <= first.size()) {
                first = span<const u8>(first.data() + bytes, first.size() - bytes);
            }
            else {
                const u32 remaining = bytes - static_cast<u32>(first.size());

                first =
                    span<const u8>(second.data() + remaining, second.size() - remaining);
                second = {};
            }

            length = static_cast<u32>(first.size() + second.size());

            return true;
        }
    };

    inline void recycle(packet_view& packet, int status = 0) {
        if (packet.recycle != nullptr) {
            auto callback = packet.recycle;

            packet.recycle = nullptr;

            callback(packet.owner, status);
        }
    }
} // namespace astia::net

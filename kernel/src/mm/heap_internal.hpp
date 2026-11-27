#pragma once

#include <astia/common.hpp>

// Free-list layout and validation shared by the allocator and its host tests.
// The checks are pure so they can be exercised without a live heap; the
// allocator wraps them with its panic path.
namespace astia::mm::heap::detail {
    /// One block: header directly precedes the payload. sizeof() is a
    /// multiple of 16, so a 16-aligned region start keeps every payload
    /// 16-byte aligned.
    struct block {
        usz    size; // payload bytes
        block* prev;
        block* next;
        bool   free;
    };

    static_assert(sizeof(block) % 16 == 0, "block header must keep 16-byte alignment");

    inline constexpr usz alignment = 16;
    inline constexpr usz max_value = ~usz{0};

    enum class block_status {
        ok,
        bad_window,
        list_cycle,
        out_of_window,
        unaligned,
        header_out_of_window,
        payload_out_of_window,
        bad_size_alignment,
        bad_prev,
        bad_next,
        bad_prev_link,
        bad_next_link,
        bad_adjacency,
    };

    inline const char* status_name(block_status status) {
        switch (status) {
        case block_status::ok:
            return "ok";
        case block_status::bad_window:
            return "heap window overflows the address space";
        case block_status::list_cycle:
            return "free-list walk exceeded its block budget";
        case block_status::out_of_window:
            return "block outside heap window";
        case block_status::unaligned:
            return "block not 16-byte aligned";
        case block_status::header_out_of_window:
            return "block header outside heap window";
        case block_status::payload_out_of_window:
            return "block payload outside heap window";
        case block_status::bad_size_alignment:
            return "block size not 16-byte aligned";
        case block_status::bad_prev:
            return "prev outside heap window";
        case block_status::bad_next:
            return "next outside heap window";
        case block_status::bad_prev_link:
            return "prev does not link back to block";
        case block_status::bad_next_link:
            return "next does not link back to block";
        case block_status::bad_adjacency:
            return "neighbouring blocks are not adjacent";
        }

        return "unknown heap corruption";
    }

    struct window {
        const u8* base  = nullptr;
        usz       bytes = 0;

        [[gnu::always_inline]] inline bool valid() const {
            const usz address = reinterpret_cast<usz>(base);

            return base != nullptr && bytes >= sizeof(block) + alignment &&
                   bytes <= max_value - address;
        }

        [[gnu::always_inline]] inline usz begin() const {
            return reinterpret_cast<usz>(base);
        }

        [[gnu::always_inline]] inline usz end() const {
            return begin() + bytes;
        }

        /// The smallest block is a header plus one aligned payload unit, so
        /// this caps a walk even when reciprocal links form a cycle.
        constexpr usz max_blocks() const {
            return bytes / (sizeof(block) + alignment);
        }
    };

    /// Fatal path for a corrupt free list. heap.cpp defines a weak trapping
    /// default so host tests do not need the kernel log/panic stack; the
    /// kernel links a strong implementation that logs and panics.
    [[noreturn]] void report_fatal(const char* where, const char* reason, u64 block,
                                   u64 base, usz bytes);

    /// True when `address` can hold a block header inside the window.
    [[gnu::always_inline]] inline bool header_fits(usz begin, usz end, usz address) {
        return address >= begin && address < end && (address % alignment) == 0 &&
               address <= max_value - sizeof(block) && address + sizeof(block) <= end;
    }

    /// Validate one block before the allocator touches its payload or links.
    /// `adjacency` is set when the caller is about to merge blocks and
    /// therefore relies on the neighbour's payload ending where the next
    /// header begins. Callers check the window and the walk budget once per
    /// walk, so this stays cheap in an unoptimised kernel build.
    [[gnu::always_inline]] inline block_status validate(const window& w, const block* b,
                                                        bool adjacency) {
        if (b == nullptr)
            return block_status::out_of_window;

        const usz begin   = w.begin();
        const usz end     = w.end();
        const usz address = reinterpret_cast<usz>(b);

        if (address < begin || address >= end)
            return block_status::out_of_window;

        if ((address % alignment) != 0)
            return block_status::unaligned;

        if (!header_fits(begin, end, address))
            return block_status::header_out_of_window;

        const usz body = address + sizeof(block);
        const usz size = b->size;

        if (size > end - body)
            return block_status::payload_out_of_window;

        // A configured region need not end on an alignment unit: only the
        // last block may keep that unaligned tail, and it must reach the end.
        if ((size % alignment) != 0 && !(b->next == nullptr && body + size == end))
            return block_status::bad_size_alignment;

        if (b->prev != nullptr) {
            if (!header_fits(begin, end, reinterpret_cast<usz>(b->prev)))
                return block_status::bad_prev;

            if (b->prev->next != b)
                return block_status::bad_prev_link;
        }

        if (b->next != nullptr) {
            const usz next = reinterpret_cast<usz>(b->next);

            if (!header_fits(begin, end, next))
                return block_status::bad_next;

            if (b->next->prev != b)
                return block_status::bad_next_link;

            if (adjacency && body + size != next)
                return block_status::bad_adjacency;
        }

        return block_status::ok;
    }
} // namespace astia::mm::heap::detail

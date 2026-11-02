#pragma once

#include "proc.hpp"
#include <astia/api/errno.h>
#include <astia/arch/layout.hpp>
#include <astia/arch/page_map.hpp>
#include <astia/common.hpp>
#include <astia/io.hpp>
#include <astia/mm/iovec.hpp>
#include <astia/optional.hpp>
#include <astia/span.hpp>

/*
 * User-pointer hygiene for the syscall boundary.
 *
 * A syscall argument that names memory belongs to the calling process:
 * it must lie in the user half of *that process's* page map, page by
 * page, before the kernel reads or writes through it.  A process map
 * shares the kernel half with the kernel, so an unvalidated
 * kernel-half address would otherwise be a free kernel-memory read or
 * write primitive from ring 3.
 *
 * The same helpers serve in-kernel ring clients and selftests that
 * drive the syscall table with kernel pointers; a caller with no
 * address space of its own is trusted as it always was (this mirrors
 * io_setup's user/kernel split).  Ring-3 callers get no such quarter.
 *
 * Validation checks: non-null (for nonzero length), entirely below
 * kernel_base, and every page translate()s in the caller's address
 * space — which is the active map for the whole duration of a syscall.
 * It does not check page *permissions* (no mprotect enforcement yet),
 * and it is TOCTOU against a concurrent munmap from another thread of
 * the same process; a blocking syscall must therefore keep no user
 * pointer across the block (copy in first, copy out after), and an
 * operation that parks for a device must pin the range instead
 * (pin_user_span) so its frames outlive the mapping.
 *
 * Header-only on purpose: proc's syscall wrappers and sys/fs code both
 * need it, and a .cpp would put proc and sys in a link cycle.
 */

namespace astia::sys {

    template <typename T>
    class validated_user_ptr;
    template <typename T>
    class user_span;

    /// An address the caller named, and the length that came with it.
    ///
    /// Deliberately not a pointer.  A user address is directly
    /// dereferenceable in ring 0 here: page_map::activate() puts the
    /// process's own table in CR3, and its map shares the kernel half, so
    /// `*(u8*) sqe.address` does not fault — it succeeds, and reads
    /// whatever the caller mapped there.  The hardware cannot catch that,
    /// so the discipline has to: a caller-named address cannot be *used*
    /// anonymously.  The two safe things to do with one are copy it now,
    /// or pin it for the flight of an operation, and the gates below are
    /// the only code that accepts a span<u8> — they are where the
    /// address finally becomes a pointer.
    ///
    /// This is a discipline, not a proof: the casts still exist, and they
    /// live in this header beside the validation that licenses them.
    /// What the type buys is that every place turning a caller-named
    /// address into a pointer names itself, and that bytes() and string()
    /// are the only ways to name one — so a value that is not an address
    /// off the ABI has to say so at one grep-able call.
    template <typename T>
    class validated_user_span {
        template <typename>
        friend class user_span;

        public:
        constexpr validated_user_span() = default;
        T* get() const {
            return reinterpret_cast<T*>(address_);
        }
        span<T> view() const {
            return span<T>(get(), length_ / sizeof(T));
        }
        span<u8> bytes() const {
            return span<u8>::bytes(address_, length_);
        }

        private:
        validated_user_span(u64 address, usize length)
            : address_(address), length_(length) {}
        u64   address_ = 0;
        usize length_  = 0;
    };

    template <typename T>
    class user_span {
        public:
        constexpr user_span() = default;
        constexpr user_span(u64 address, usize count)
            : address_(address), count_(count) {}

        constexpr u64 address() const {
            return address_;
        }
        constexpr usize count() const {
            return count_;
        }
        constexpr span<u8> bytes() const {
            return span<u8>::bytes(address_, sizeof(T) * count_);
        }
        optional<validated_user_span<T>> validate() const;

        private:
        u64   address_ = 0;
        usize count_   = 0;
    };

    /// A typed borrowed view formed only after page-map validation.
    template <typename T>
    class validated_user_ptr {
        template <typename>
        friend class user_ptr;

        public:
        constexpr validated_user_ptr() = default;
        T* get() const {
            return reinterpret_cast<T*>(address_);
        }
        span<T> view(usize count = 1) const {
            return span<T>(get(), count);
        }
        span<u8> bytes(usize count = 1) const {
            return span<u8>::bytes(address_, sizeof(T) * count);
        }

        private:
        explicit validated_user_ptr(u64 address) : address_(address) {}
        u64 address_;
    };

    /// Typed userspace address token.  It deliberately cannot be
    /// dereferenced or converted to a kernel pointer; callers must choose a
    /// bounded span<u8> before copying or pinning memory.
    template <typename T>
    class user_ptr {
        public:
        constexpr user_ptr() = default;
        explicit constexpr user_ptr(u64 address) : address_(address) {}

        constexpr u64 address() const {
            return address_;
        }
        constexpr bool null() const {
            return address_ == 0;
        }
        constexpr span<u8> bytes(usize count = 1) const {
            return span<u8>::bytes(address_, sizeof(T) * count);
        }
        constexpr span<u8> string() const {
            static_assert(sizeof(T) == 1, "string pointers must point to bytes");
            return span<u8>::string(address_);
        }
        optional<validated_user_ptr<T>> validate(usize count = 1) const;

        private:
        u64 address_ = 0;
    };

    /// True if `range` may be accessed by the current caller.  A caller with
    /// no address space of its own (the kernel process, in-kernel ring
    /// clients, selftests driving the syscall table) passes kernel pointers
    /// through these same paths and is trusted exactly as it always was.
    /// Everyone else is a ring-3 process: the range must lie entirely below
    /// kernel_base and every page must translate in the caller's address
    /// space.  A zero length is vacuously valid; a null base with nonzero
    /// length is not.
    inline bool user_span_ok(span<u8> range) {
        if (range.length() == 0) {
            return true;
        }
        const u64 base = range.address();
        if (base == 0) {
            return false;
        }
        const proc::process*  owner = proc::current_process();
        const arch::page_map* space = owner == nullptr ? nullptr : owner->address_space();
        if (space == nullptr) {
            return true; // kernel client: kernel pointers, as before
        }
        if (base >= arch::kernel_base || range.length() > arch::kernel_base - base) {
            return false;
        }
        const u64 first = base & ~mm::page_mask;
        const u64 last  = (base + range.length() - 1) & ~mm::page_mask;
        for (u64 page = first;; page += mm::page_size) {
            if (space->translate(page) == arch::page_map::invalid) {
                return false;
            }
            if (page == last) {
                return true;
            }
        }
    }

    template <typename T>
    optional<validated_user_ptr<T>> user_ptr<T>::validate(usize count) const {
        if (count > (~usize{0} / sizeof(T)))
            return error_t::erange;
        if (!user_span_ok(bytes(count)))
            return error_t::efault;
        return validated_user_ptr<T>(address_);
    }

    template <typename T>
    optional<validated_user_span<T>> user_span<T>::validate() const {
        if (count_ > (~usize{0} / sizeof(T)))
            return error_t::erange;
        if (!user_span_ok(bytes()))
            return error_t::efault;
        return validated_user_span<T>(address_, sizeof(T) * count_);
    }

    /// Snapshot one entry of an asynchronous operation's buffer list: 0, or a
    /// negated errno.  user_span_ok() only proves the range is readable
    /// *now*; an operation that parks for a device needs the frames to still
    /// be allocated, and still be the frames the caller reads, when the device
    /// gets to them — possibly after the syscall returned and another thread
    /// of the process unmapped the range.  `out` then holds the pinned entry
    /// (see mm::iovec), whose window folds the range's in-page offset
    /// back in; `direction` decides whether a CoW split of those frames has to
    /// be refused.  A caller with no address space has nothing to snapshot, so
    /// its range is recorded as a plain kernel-memory entry: it cannot move,
    /// but the operation still has to walk it like every other entry.
    inline long pin_user_span(span<u8> range, io_direction direction, mm::iovec& out) {
        if (range.empty()) {
            return 0;
        }
        proc::process* owner = proc::current_process();
        if (owner == nullptr || owner->address_space() == nullptr) {
            // Kernel client: kernel memory, which cannot be unmapped.  This
            // is the one range the kernel owns rather than the caller, and
            // it is the only cast of the address here that is not a user
            // pointer at all.
            return out.append_kernel(reinterpret_cast<void*>(range.address()),
                                     range.length())
                       ? 0
                       : -ENOMEM;
        }
        return owner->pin_user(range.address(), range.length(),
                               direction == io_direction::input, out);
    }

    /// Copy out of the kernel into a validated user range.  The count comes
    /// from the range; `source` must be at least that long (a kernel local,
    /// so that much is the caller's own promise).  Returns false, writing
    /// nothing, if the range fails validation.
    inline bool copy_out(span<u8> destination, const void* source) {
        // Besides avoiding a meaningless source dereference, this keeps the
        // page walk below from turning an empty range into address-1.  Empty
        // optional output slots are valid and must be a no-op.
        if (destination.empty()) {
            return true;
        }
        if (!user_span_ok(destination)) {
            return false;
        }
        // A forked child's private writable mappings are read-only until the
        // child writes them.  This copy is performed in ring 0, so hardware
        // cannot enter the ring-3 page-fault path if the destination still
        // carries that CoW protection.  Resolve each page explicitly before
        // taking the user address as a writable pointer.
        proc::process* owner = proc::current_process();
        if (owner != nullptr && owner->address_space() != nullptr) {
            const u64 first = destination.address() & ~mm::page_mask;
            const u64 last =
                (destination.address() + destination.length() - 1) & ~mm::page_mask;
            for (u64 page = first;; page += mm::page_size) {
                if (!owner->address_space()->writable(page) &&
                    !owner->cow_fault(page, true)) {
                    return false;
                }
                if (page == last) {
                    break;
                }
            }
        }
        auto*       to   = reinterpret_cast<u8*>(destination.address());
        const auto* from = static_cast<const u8*>(source);
        for (usize i = 0; i < destination.length(); ++i) {
            to[i] = from[i];
        }
        return true;
    }

    /// Copy in from a validated user range into the kernel.
    inline bool copy_in(void* destination, span<u8> source) {
        if (!user_span_ok(source)) {
            return false;
        }
        auto*       to   = static_cast<u8*>(destination);
        const auto* from = reinterpret_cast<const u8*>(source.address());
        for (usize i = 0; i < source.length(); ++i) {
            to[i] = from[i];
        }
        return true;
    }

    /// Copy a NUL-terminated string out of the caller's memory into a
    /// kernel buffer of `capacity` bytes (terminator included).  `source`
    /// is a string range, so its length is not consulted: the walk ends at
    /// the terminator or at `capacity`, whichever comes first.  For a
    /// ring-3 caller, pages are validated lazily as the walk crosses them,
    /// so a string at the top of a mapped region is not rejected for the
    /// unmapped page above it; a kernel client gets a plain bounded copy.
    /// Fails if any page is unmapped or the terminator is not found within
    /// the capacity bound.
    inline bool copy_in_string(span<char> destination, span<u8> source) {
        const usize capacity = destination.size();
        if (capacity == 0 || source.null()) {
            return false;
        }
        const proc::process*  owner = proc::current_process();
        const arch::page_map* space = owner == nullptr ? nullptr : owner->address_space();
        const auto*           from  = reinterpret_cast<const u8*>(source.address());
        u64                   byte  = source.address();
        if (space == nullptr) { // kernel client: bounded straight copy
            for (usize i = 0; i < capacity; ++i) {
                destination.data()[i] = static_cast<char>(from[i]);
                if (destination.data()[i] == '\0') {
                    return true;
                }
            }
            return false;
        }
        if (byte >= arch::kernel_base) {
            return false;
        }
        if (space->translate(byte & ~mm::page_mask) == arch::page_map::invalid) {
            return false;
        }
        for (usize i = 0; i < capacity; ++i, ++byte) {
            if (byte >= arch::kernel_base) {
                return false;
            }
            if ((byte & mm::page_mask) == 0 &&
                space->translate(byte) == arch::page_map::invalid) {
                return false;
            }
            destination.data()[i] = static_cast<char>(from[i]);
            if (destination.data()[i] == '\0') {
                return true;
            }
        }
        return false; // no terminator within capacity
    }

    inline bool copy_in_string(span<char> destination, user_ptr<char> source) {
        return copy_in_string(destination, source.string());
    }

} // namespace astia::sys

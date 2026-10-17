#pragma once

#include "proc.hpp"
#include <astia/api/errno.h>
#include <astia/arch/layout.hpp>
#include <astia/arch/page_map.hpp>
#include <astia/common.hpp>
#include <astia/io.hpp>
#include <astia/mm/pinned_iovec.hpp>

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
 * (pin_user_range) so its frames outlive the mapping.
 *
 * Header-only on purpose: proc's syscall wrappers and sys/fs code both
 * need it, and a .cpp would put proc and sys in a link cycle.
 */

namespace astia::sys {

    /// True if [address, address + length) may be accessed by the
    /// current caller.  A caller with no address space of its own (the
    /// kernel process, in-kernel ring clients, selftests driving the
    /// syscall table) passes kernel pointers through these same paths
    /// and is trusted exactly as it always was.  Everyone else is a
    /// ring-3 process: the range must lie entirely below kernel_base
    /// and every page must translate in the caller's address space.
    /// A zero length is vacuously valid; a null base with nonzero
    /// length is not.
    inline bool user_range_ok(const void* address, usize length) {
        if (length == 0) {
            return true;
        }
        const u64 base = reinterpret_cast<u64>(address);
        if (base == 0) {
            return false;
        }
        const proc::process*  owner = proc::current_process();
        const arch::page_map* space = owner == nullptr ? nullptr : owner->address_space();
        if (space == nullptr) {
            return true; // kernel client: kernel pointers, as before
        }
        if (base >= arch::kernel_base || length > arch::kernel_base - base) {
            return false;
        }
        const u64 first = base & ~u64{0xfff};
        const u64 last  = (base + length - 1) & ~u64{0xfff};
        for (u64 page = first;; page += 4096) {
            if (space->translate(page) == arch::page_map::invalid) {
                return false;
            }
            if (page == last) {
                return true;
            }
        }
    }

    /// Snapshot one entry of an asynchronous operation's buffer list: 0, or a
    /// negated errno.  user_range_ok() only proves the range is readable
    /// *now*; an operation that parks for a device needs the frames to still
    /// be allocated, and still be the frames the caller reads, when the device
    /// gets to them — possibly after the syscall returned and another thread
    /// of the process unmapped the range.  `out` then holds the pinned entry
    /// (see mm::pinned_iovec), whose window folds the range's in-page offset
    /// back in; `direction` decides whether a CoW split of those frames has to
    /// be refused.  A caller with no address space has nothing to snapshot, so
    /// its range is recorded as a plain kernel-memory entry: it cannot move,
    /// but the operation still has to walk it like every other entry.
    inline long pin_user_range(const void* address, usize length, io_direction direction,
                               mm::pinned_iovec& out) {
        if (length == 0) {
            return 0;
        }
        proc::process* owner = proc::current_process();
        if (owner == nullptr || owner->address_space() == nullptr) {
            // Kernel client: kernel memory, which cannot be unmapped.
            return out.append_kernel(const_cast<void*>(address), length) ? 0 : -ENOMEM;
        }
        return owner->pin_user(reinterpret_cast<u64>(address), length,
                               direction == io_direction::input, out);
    }

    /// Copy out of the kernel into a validated user range.  Returns
    /// false (writing nothing) if the range fails validation.
    inline bool copy_out(void* user, const void* source, usize length) {
        if (!user_range_ok(user, length)) {
            return false;
        }
        auto*       to   = static_cast<u8*>(user);
        const auto* from = static_cast<const u8*>(source);
        for (usize i = 0; i < length; ++i) {
            to[i] = from[i];
        }
        return true;
    }

    /// Copy in from a validated user range into the kernel.
    inline bool copy_in(void* destination, const void* user, usize length) {
        if (!user_range_ok(user, length)) {
            return false;
        }
        auto*       to   = static_cast<u8*>(destination);
        const auto* from = static_cast<const u8*>(user);
        for (usize i = 0; i < length; ++i) {
            to[i] = from[i];
        }
        return true;
    }

    /// Copy a NUL-terminated string out of the caller's memory into a
    /// kernel buffer of `capacity` bytes (terminator included).  For a
    /// ring-3 caller, pages are validated lazily as the walk crosses
    /// them, so a string at the top of a mapped region is not rejected
    /// for the unmapped page above it; a kernel client gets a plain
    /// bounded copy.  Fails if any page is unmapped or the terminator
    /// is not found within the capacity bound.
    inline bool copy_in_string(char* destination, usize capacity, const void* user) {
        if (capacity == 0) {
            return false;
        }
        const proc::process*  owner = proc::current_process();
        const arch::page_map* space = owner == nullptr ? nullptr : owner->address_space();
        const auto*           from  = static_cast<const u8*>(user);
        u64                   byte  = reinterpret_cast<u64>(from);
        if (byte == 0) {
            return false;
        }
        if (space == nullptr) { // kernel client: bounded straight copy
            for (usize i = 0; i < capacity; ++i) {
                destination[i] = static_cast<char>(from[i]);
                if (destination[i] == '\0') {
                    return true;
                }
            }
            return false;
        }
        if (byte >= arch::kernel_base) {
            return false;
        }
        if (space->translate(byte & ~u64{0xfff}) == arch::page_map::invalid) {
            return false;
        }
        for (usize i = 0; i < capacity; ++i, ++byte) {
            if (byte >= arch::kernel_base) {
                return false;
            }
            if ((byte & 0xfff) == 0 &&
                space->translate(byte) == arch::page_map::invalid) {
                return false;
            }
            destination[i] = static_cast<char>(from[i]);
            if (destination[i] == '\0') {
                return true;
            }
        }
        return false; // no terminator within capacity
    }

} // namespace astia::sys

#pragma once

#include "layout.hpp"
#include "page_map.hpp"
#include <astia/common.hpp>
#include "proc.hpp"

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
 * the same process; blocking syscalls must therefore keep no user
 * pointer across the block (copy in first, copy out after).
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
    inline auto user_range_ok(const void* address, usize length) -> bool {
        if (length == 0) {
            return true;
        }
        const u64 base = reinterpret_cast<u64>(address);
        if (base == 0) {
            return false;
        }
        const proc::process* owner = proc::current_process();
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

    /// Copy out of the kernel into a validated user range.  Returns
    /// false (writing nothing) if the range fails validation.
    inline auto copy_out(void* user, const void* source, usize length) -> bool {
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
    inline auto copy_in(void* destination, const void* user, usize length) -> bool {
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
    inline auto copy_in_string(char* destination, usize capacity, const void* user)
        -> bool {
        if (capacity == 0) {
            return false;
        }
        const proc::process* owner = proc::current_process();
        const arch::page_map* space = owner == nullptr ? nullptr : owner->address_space();
        const auto*           from = static_cast<const u8*>(user);
        u64                   byte = reinterpret_cast<u64>(from);
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

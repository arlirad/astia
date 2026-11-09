#pragma once

#include "../../user_layout.hpp"
#include "user_mem.hpp"
#include <astia/arch/layout.hpp>

namespace astia::proc::syscall {

    inline bool valid_thread_start(u64 entry, u64 stack_top) {
        constexpr u64 stack_alignment_bytes = thread_stack_alignment_bytes;
        constexpr u64 stack_alignment_mask  = thread_stack_alignment_mask;

        return entry != 0 && stack_top != 0 && entry < arch::kernel_base &&
               stack_top < arch::kernel_base && (stack_top & stack_alignment_mask) == 0 &&
               stack_top >= stack_alignment_bytes &&
               sys::user_ptr<u8>(entry).validate() &&
               sys::user_ptr<u8>(stack_top - stack_alignment_bytes)
                   .validate(stack_alignment_bytes);
    }

    inline bool valid_thread_stack_metadata(u64 stack_base, u64 stack_size,
                                            u64 stack_top) {
        if (stack_base == 0 || stack_size == 0 ||
            stack_size > static_cast<u64>(~usize{0}) ||
            stack_base > ~u64{0} - stack_size ||
            stack_base + stack_size != stack_top ||
            (stack_base & thread_stack_alignment_mask) != 0)
            return false;
        return static_cast<bool>(
            sys::user_span<u8>(stack_base, static_cast<usize>(stack_size)).validate());
    }

} // namespace astia::proc::syscall

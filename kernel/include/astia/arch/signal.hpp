#pragma once

#include <astia/common.hpp>

#include <concepts>

namespace astia::arch {
    class syscall_return;
    struct interrupt_frame;

    /// Upper bound on a port's saved signal-context ABI.  Generic code keeps
    /// an opaque buffer of this size; a port asserts its own struct fits, so
    /// no generic translation unit ever needs the concrete type or its size.
    inline constexpr usz max_signal_context_bytes = 256;

    /// Exact size of the port's user signal-context ABI.
    usz signal_context_bytes();

    /// Result of validating an opaque saved context copied from user memory.
    struct signal_restore_state {
        bool valid     = false;
        bool faults    = false;
        u32  blocked   = 0;
        bool alt_stack = false;
    };

    /// Encode the interrupted syscall exit into `storage`.
    void capture_signal_context(void* storage, const syscall_return& image, i64 result,
                                u32 blocked, bool alt_stack);

    /// Encode a faulting ring-3 context into `storage`.
    void capture_fault_signal_context(void* storage, const interrupt_frame& frame,
                                      u32 blocked, bool alt_stack);

    /// Validate an opaque context and report what restoring it implies.
    signal_restore_state validate_signal_context(const void* storage);

    /// The port contract: these operations exist for the selected port.
    struct signal_context_port_tag {};

    template <typename = signal_context_port_tag>
    concept signal_context_port =
        requires(void* storage, const void* const_storage, syscall_return& image,
                 i64 result, u32 blocked, bool alt) {
            { signal_context_bytes() } -> std::convertible_to<usz>;
            { capture_signal_context(storage, image, result, blocked, alt) };
            {
                validate_signal_context(const_storage)
            } -> std::same_as<signal_restore_state>;
        };
    static_assert(signal_context_port<>);
} // namespace astia::arch

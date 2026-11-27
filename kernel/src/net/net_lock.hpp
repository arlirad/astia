#pragma once

#include <astia/arch/spinlock_guard.hpp>
#include <astia/core/spinlock.hpp>

namespace astia::net::detail {
#if defined(ASTIA_HOST_TEST)
    /// Host tests run in userspace, where masking interrupts is illegal; the
    /// plain spinlock still serializes them.
    using lock_guard = core::spinlock_guard;
#else
    using lock_guard = arch::interrupt_spinlock_guard;
#endif
} // namespace astia::net::detail

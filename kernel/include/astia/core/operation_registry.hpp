#pragma once

#include <astia/common.hpp>
#include <astia/core/cancellation.hpp>

namespace astia::core {

    using operation_exit_hook = void (*)(void* object, cancel_reason reason);
    using operation_refs_zero = void (*)(void* object);

    bool register_operation(void* object, u64 process, u64 thread, volatile u32* refs,
                            operation_exit_hook hook,
                            operation_refs_zero refs_zero  = nullptr,
                            volatile u8*        zero_guard = nullptr);
    void unregister_operation(void* object) noexcept;

    void cancel_operations_for_process(u64 process, cancel_reason reason);
    void cancel_operations_for_thread(u64 process, u64 thread, cancel_reason reason);

    u32 operation_registry_count() noexcept;
    u32 operation_registry_capacity() noexcept;

    bool defer_operation_destroy(void* object, void (*destroy)(void* object)) noexcept;
    void reap_operations() noexcept;
    u32  operation_reaper_pending() noexcept;

    u64 allocate_lifecycle_id() noexcept;

} // namespace astia::core

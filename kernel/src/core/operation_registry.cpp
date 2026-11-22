#include <astia/core/operation_registry.hpp>
#include <astia/core/spinlock.hpp>
#include <astia/resource_limits.hpp>

namespace astia::core {

    namespace {
        constexpr u32 registry_capacity = resource_limits::queue::operation_registry;
        constexpr u32 reaper_capacity   = resource_limits::queue::operation_reaper;

        struct registry_entry {
            void*               object    = nullptr;
            u64                 process   = 0;
            u64                 thread    = 0;
            volatile u32*       refs      = nullptr;
            operation_exit_hook hook      = nullptr;
            operation_refs_zero refs_zero = nullptr;
            bool                signaled  = false;
        };

        struct destroy_entry {
            void* object           = nullptr;
            void (*destroy)(void*) = nullptr;
        };

        registry_entry registry[registry_capacity]{};
        destroy_entry  reaper[reaper_capacity]{};
        u32            reaper_count = 0;
        core::spinlock lock{};

        registry_entry* find_entry(void* object) {
            for (registry_entry& entry : registry) {
                if (entry.object == object)
                    return &entry;
            }
            return nullptr;
        }

        void cancel_matching(u64 process, u64 thread, bool match_thread,
                             cancel_reason reason) {
            for (;;) {
                void*               object    = nullptr;
                operation_exit_hook hook      = nullptr;
                operation_refs_zero refs_zero = nullptr;
                volatile u32*       refs      = nullptr;
                {
                    core::spinlock_guard guard(lock);
                    for (registry_entry& entry : registry) {
                        if (entry.object == nullptr || entry.signaled ||
                            entry.process != process ||
                            (match_thread && entry.thread != thread))
                            continue;
                        entry.signaled = true;
                        object         = entry.object;
                        hook           = entry.hook;
                        refs_zero      = entry.refs_zero;
                        refs           = entry.refs;
                        __atomic_add_fetch(refs, 1, __ATOMIC_ACQ_REL);
                        break;
                    }
                }
                if (object == nullptr)
                    return;
                hook(object, reason);
                if (__atomic_sub_fetch(refs, 1, __ATOMIC_ACQ_REL) == 0 &&
                    refs_zero != nullptr)
                    refs_zero(object);
            }
        }

        u64 next_lifecycle_id = 1;
    }

    bool register_operation(void* object, u64 process, u64 thread, volatile u32* refs,
                            operation_exit_hook hook, operation_refs_zero refs_zero) {
        if (object == nullptr || refs == nullptr || hook == nullptr || process == 0)
            return false;
        core::spinlock_guard guard(lock);
        if (find_entry(object) != nullptr)
            return false;
        for (registry_entry& entry : registry) {
            if (entry.object != nullptr)
                continue;
            entry.object    = object;
            entry.process   = process;
            entry.thread    = thread;
            entry.refs      = refs;
            entry.hook      = hook;
            entry.refs_zero = refs_zero;
            entry.signaled  = false;
            __atomic_store_n(refs, 0, __ATOMIC_RELEASE);
            return true;
        }
        return false;
    }

    void unregister_operation(void* object) noexcept {
        core::spinlock_guard guard(lock);
        registry_entry*      entry = find_entry(object);
        if (entry == nullptr)
            return;
        entry->object    = nullptr;
        entry->process   = 0;
        entry->thread    = 0;
        entry->refs      = nullptr;
        entry->hook      = nullptr;
        entry->refs_zero = nullptr;
        entry->signaled  = false;
    }

    void cancel_operations_for_process(u64 process, cancel_reason reason) {
        cancel_matching(process, 0, false, reason);
    }

    void cancel_operations_for_thread(u64 process, u64 thread, cancel_reason reason) {
        cancel_matching(process, thread, true, reason);
    }

    u32 operation_registry_count() noexcept {
        core::spinlock_guard guard(lock);
        u32                  count = 0;
        for (registry_entry& entry : registry) {
            if (entry.object != nullptr)
                ++count;
        }
        return count;
    }

    u32 operation_registry_capacity() noexcept {
        return registry_capacity;
    }

    bool defer_operation_destroy(void* object, void (*destroy)(void* object)) noexcept {
        if (object == nullptr || destroy == nullptr)
            return false;
        core::spinlock_guard guard(lock);
        if (reaper_count == reaper_capacity)
            return false;
        reaper[reaper_count].object  = object;
        reaper[reaper_count].destroy = destroy;
        ++reaper_count;
        return true;
    }

    void reap_operations() noexcept {
        destroy_entry pending[reaper_capacity];
        u32           count = 0;
        {
            core::spinlock_guard guard(lock);
            for (u32 i = 0; i < reaper_count; ++i) {
                pending[i]        = reaper[i];
                reaper[i].object  = nullptr;
                reaper[i].destroy = nullptr;
            }
            count        = reaper_count;
            reaper_count = 0;
        }
        for (u32 i = 0; i < count; ++i)
            pending[i].destroy(pending[i].object);
    }

    u32 operation_reaper_pending() noexcept {
        core::spinlock_guard guard(lock);
        return reaper_count;
    }

    u64 allocate_lifecycle_id() noexcept {
        u64 value = __atomic_add_fetch(&next_lifecycle_id, 1, __ATOMIC_RELAXED);
        if (value == 0)
            value = __atomic_add_fetch(&next_lifecycle_id, 1, __ATOMIC_RELAXED);
        return value;
    }

} // namespace astia::core

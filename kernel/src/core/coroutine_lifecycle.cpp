#include <astia/core/coroutine_lifecycle.hpp>
#include <astia/core/spinlock.hpp>
#include <astia/resource_limits.hpp>

#if defined(ASTIA_HOST_COROUTINE_LIFECYCLE)
namespace {
    using lifecycle_guard = astia::core::spinlock_guard;
}
#else
#include <astia/arch/cpu.hpp>

namespace {
    using lifecycle_guard = astia::arch::interrupt_spinlock_guard;
}
#endif

namespace astia::core {

    namespace {
        constexpr u32 slot_capacity = resource_limits::coroutine::lifecycle_slots;

        enum class slot_state : u8 {
            free,
            active,
            destroying,
            tombstone,
        };

        struct slot {
            std::coroutine_handle<>    handle{};
            coroutine_destroy_observer observer = nullptr;
            void*                      context  = nullptr;
            u32                        refs     = 0;
            u8                         state    = static_cast<u8>(slot_state::free);
            u8                         pending  = 0;
        };

        slot           slots[slot_capacity]{};
        core::spinlock lock{};

        usize slot_index(std::coroutine_handle<> handle) {
            const usize address = reinterpret_cast<usize>(handle.address());
            return (address >> 4) % slot_capacity;
        }

        slot* find_slot(std::coroutine_handle<> handle) {
            const usize start = slot_index(handle);
            for (u32 step = 0; step < slot_capacity; ++step) {
                slot&      candidate = slots[(start + step) % slot_capacity];
                const auto state     = static_cast<slot_state>(
                    __atomic_load_n(&candidate.state, __ATOMIC_ACQUIRE));
                if (state == slot_state::free)
                    return nullptr;
                if (state != slot_state::tombstone &&
                    candidate.handle.address() == handle.address())
                    return &candidate;
            }
            return nullptr;
        }

        slot* allocate_slot(std::coroutine_handle<> handle) {
            const usize start = slot_index(handle);
            for (u32 step = 0; step < slot_capacity; ++step) {
                slot&      candidate = slots[(start + step) % slot_capacity];
                const auto state     = static_cast<slot_state>(
                    __atomic_load_n(&candidate.state, __ATOMIC_ACQUIRE));
                if (state == slot_state::free || state == slot_state::tombstone) {
                    candidate.handle   = handle;
                    candidate.observer = nullptr;
                    candidate.context  = nullptr;
                    candidate.refs     = 0;
                    candidate.pending  = 0;
                    __atomic_store_n(&candidate.state,
                                     static_cast<u8>(slot_state::active),
                                     __ATOMIC_RELEASE);
                    return &candidate;
                }
            }
            return nullptr;
        }

        void release_slot(slot& value) {
            value.handle   = {};
            value.observer = nullptr;
            value.context  = nullptr;
            value.refs     = 0;
            value.pending  = 0;
            __atomic_store_n(&value.state, static_cast<u8>(slot_state::tombstone),
                             __ATOMIC_RELEASE);
        }

        void destroy_and_release(std::coroutine_handle<>    handle,
                                 coroutine_destroy_observer observer, void* context) {
            handle.destroy();
            {
                lifecycle_guard guard(lock);
                slot*           value = find_slot(handle);
                if (value != nullptr)
                    release_slot(*value);
            }
            if (observer != nullptr)
                observer(context);
        }

    }

    bool resume_begin(std::coroutine_handle<> handle) noexcept {
        if (!handle)
            return false;
        lifecycle_guard guard(lock);
        slot*           value = find_slot(handle);
        if (value == nullptr)
            value = allocate_slot(handle);
        if (value == nullptr)
            return false;
        if (static_cast<slot_state>(__atomic_load_n(&value->state, __ATOMIC_ACQUIRE)) !=
            slot_state::active)
            return false;
        ++value->refs;
        return true;
    }

    void resume_end(std::coroutine_handle<> handle) noexcept {
        if (!handle)
            return;
        std::coroutine_handle<>    destroy{};
        coroutine_destroy_observer observer = nullptr;
        void*                      context  = nullptr;
        {
            lifecycle_guard guard(lock);
            slot*           value = find_slot(handle);
            if (value == nullptr || value->refs == 0)
                return;
            --value->refs;
            if (value->refs != 0 || value->pending == 0)
                return;
            __atomic_store_n(&value->state, static_cast<u8>(slot_state::destroying),
                             __ATOMIC_RELEASE);
            destroy  = value->handle;
            observer = value->observer;
            context  = value->context;
        }
        destroy_and_release(destroy, observer, context);
    }

    bool resume_now(std::coroutine_handle<> handle) noexcept {
        if (!resume_begin(handle))
            return false;
        handle.resume();
        resume_end(handle);
        return true;
    }

    bool destroy_coroutine(std::coroutine_handle<>    handle,
                           coroutine_destroy_observer observer, void* context) noexcept {
        if (!handle)
            return true;
        {
            lifecycle_guard guard(lock);
            slot*           value = find_slot(handle);
            if (value == nullptr)
                value = allocate_slot(handle);
            if (value == nullptr)
                return false;
            if (static_cast<slot_state>(__atomic_load_n(
                    &value->state, __ATOMIC_ACQUIRE)) == slot_state::destroying)
                return false;
            if (value->refs != 0) {
                value->pending  = 1;
                value->observer = observer;
                value->context  = context;
                return false;
            }
            __atomic_store_n(&value->state, static_cast<u8>(slot_state::destroying),
                             __ATOMIC_RELEASE);
        }
        destroy_and_release(handle, nullptr, nullptr);
        return true;
    }

    u32 coroutine_lifecycle_capacity() noexcept {
        return slot_capacity;
    }

    u32 coroutine_lifecycle_inflight() noexcept {
        u32             count = 0;
        lifecycle_guard guard(lock);
        for (slot& value : slots) {
            if (static_cast<slot_state>(__atomic_load_n(
                    &value.state, __ATOMIC_ACQUIRE)) == slot_state::active)
                count += value.refs;
        }
        return count;
    }

    u32 coroutine_lifecycle_pending() noexcept {
        u32             count = 0;
        lifecycle_guard guard(lock);
        for (slot& value : slots) {
            if (static_cast<slot_state>(__atomic_load_n(
                    &value.state, __ATOMIC_ACQUIRE)) == slot_state::active &&
                value.pending != 0)
                ++count;
        }
        return count;
    }

} // namespace astia::core

#include <astia/api/errno.h>

#include "syscall.hpp"

namespace astia::sys {

    extern "C" {
    extern const syscall_descriptor* const __start_astia_syscall_handlers[];
    extern const syscall_descriptor* const __stop_astia_syscall_handlers[];
    }

    namespace {
        constexpr usize table_capacity = syscall_table_capacity;

        /// O(1) dispatch table, built once from the link-section registrations.
        const syscall_descriptor* table[table_capacity] = {};
        bool                      registry_ready        = false;

        usize registry_size() {
            return static_cast<usize>(__stop_astia_syscall_handlers -
                                      __start_astia_syscall_handlers);
        }

        bool build_table() {
            for (usize i = 0; i < registry_size(); ++i) {
                const syscall_descriptor* entry = __start_astia_syscall_handlers[i];
                if (entry == nullptr || entry->invoke == nullptr)
                    return false;
                const u32 index = static_cast<u32>(entry->id);
                if (index >= table_capacity || table[index] != nullptr)
                    return false;
                table[index] = entry;
            }
            registry_ready = true;
            return true;
        }
    } // namespace

    bool validate_registry() {
        return registry_ready || build_table();
    }

    i64 dispatch(syscall_id id, u64 a0, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5,
                 arch::syscall_return& image) {
        if (!registry_ready)
            (void) build_table();

        const u32 index = static_cast<u32>(id);
        if (index >= table_capacity || table[index] == nullptr)
            return -ENOSYS;

        core::completion<i64> completion;
        table[index]->invoke(a0, a1, a2, a3, a4, a5, image, completion);
        if (!completion.done())
            (void) completion.cancel();
        const auto value = completion.await_resume();
        return value ? value.value : -EINTR;
    }

} // namespace astia::sys

#include <astia/api/errno.h>

#include "../proc/task_run.hpp"
#include "syscall.hpp"

namespace astia::sys {
#if !defined(ASTIA_MODULE_HOST_TEST)
    extern "C" {
    extern const syscall_descriptor* const __start_astia_syscall_handlers[];
    extern const syscall_descriptor* const __stop_astia_syscall_handlers[];
    }
#endif

    namespace {
        constexpr usz table_capacity = syscall_table_capacity;

        const syscall_descriptor* table[table_capacity] = {};
        bool                      registry_ready        = false;

#if !defined(ASTIA_MODULE_HOST_TEST)
        usz registry_size() {
            return static_cast<usz>(__stop_astia_syscall_handlers -
                                    __start_astia_syscall_handlers);
        }

        bool build_table() {
            for (usz i = 0; i < registry_size(); ++i) {
                const syscall_descriptor* entry = __start_astia_syscall_handlers[i];
                if (entry == nullptr)
                    return false;

                const u32 index = static_cast<u32>(entry->id);
                if (index >= table_capacity)
                    return false;

                if ((entry->sync == nullptr) == (entry->async == nullptr))
                    return false;

                if (table[index] != nullptr)
                    return false;

                table[index] = entry;
            }

            registry_ready = true;

            return true;
        }
#endif
    } // namespace

#if defined(ASTIA_MODULE_HOST_TEST)
    // The host test links the module handlers directly and registers them
    // here.  Section-based discovery relies on the linker keeping the
    // __start_/__stop_ range, which not every host toolchain honours.
    bool register_descriptor(const syscall_descriptor* entry) {
        if (entry == nullptr)
            return false;

        const u32 index = static_cast<u32>(entry->id);

        if (index >= table_capacity || table[index] != nullptr)
            return false;

        table[index]   = entry;
        registry_ready = true;

        return true;
    }
#endif

    bool validate_registry() {
#if defined(ASTIA_MODULE_HOST_TEST)
        return registry_ready;
#else
        return registry_ready || build_table();
#endif
    }

    i64 dispatch(syscall_id id, u64 a0, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5,
                 arch::syscall_return& image) {
#if !defined(ASTIA_MODULE_HOST_TEST)
        if (!registry_ready)
            (void) build_table();
#endif

        const u32 index = static_cast<u32>(id);
        if (index >= table_capacity)
            return -ENOSYS;

        const syscall_descriptor* entry = table[index];
        if (entry == nullptr)
            return -ENOSYS;

        if (entry->sync != nullptr)
            return entry->sync(a0, a1, a2, a3, a4, a5, image);

        return proc::run_task(entry->async(a0, a1, a2, a3, a4, a5, image));
    }
} // namespace astia::sys

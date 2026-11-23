#include <astia/api/errno.h>

#include "syscall.hpp"

namespace astia::sys {
    extern "C" {
    extern const syscall_descriptor __start_astia_syscall_handlers[];
    extern const syscall_descriptor __stop_astia_syscall_handlers[];
    }

    namespace {
        usize registry_size() {
            return static_cast<usize>(__stop_astia_syscall_handlers -
                                      __start_astia_syscall_handlers);
        }
    } // namespace

    i64 dispatch(syscall_id id, u64 a0, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5,
                 arch::syscall_return& image) {
        const usize count = registry_size();

        for (usize i = 0; i < count; ++i) {
            const syscall_descriptor& entry = __start_astia_syscall_handlers[i];
            if (entry.id != id)
                continue;

            core::completion<i64> completion;
            entry.invoke(a0, a1, a2, a3, a4, a5, image, completion);
            if (!completion.done())
                (void) completion.cancel();

            const auto value = completion.await_resume();
            return value ? value.value : -EINTR;
        }

        return -ENOSYS;
    }

    bool validate_registry() {
        const usize count = registry_size();

        for (usize i = 0; i < count; ++i) {
            const syscall_descriptor& entry = __start_astia_syscall_handlers[i];
            if (entry.invoke == nullptr ||
                static_cast<u32>(entry.id) >= syscall_table_capacity)
                return false;

            for (usize j = i + 1; j < count; ++j) {
                if (entry.id == __start_astia_syscall_handlers[j].id)
                    return false;
            }
        }

        return true;
    }
} // namespace astia::sys

#include "syscall.hpp"

#include <astia/api/errno.h>

namespace astia::sys {

    namespace {
        syscall_exit_hook exit_hook = nullptr;
    }

    bool register_exit_hook(syscall_exit_hook hook) {
        if (hook == nullptr || exit_hook != nullptr) {
            return false;
        }
        exit_hook = hook;
        return true;
    }

    void invoke_exit_hook(arch::syscall_return& image, i64 result) {
        if (exit_hook != nullptr)
            exit_hook(image, result);
    }

    i64 dispatch(syscall_id id, u64 a0, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5,
                 arch::syscall_return& image) {
        i64 result = -ENOSYS;
        (void) production_dispatch(id, a0, a1, a2, a3, a4, a5, image, result);
        return result;
    }

} // namespace astia::sys
#include "syscall.hpp"

#include <astia/api/errno.h>

namespace astia::sys {
    namespace {
        syscall_exit_hook exit_hook = nullptr;
    }

    bool register_exit_hook(syscall_exit_hook hook) {
        if (hook == nullptr || exit_hook != nullptr)
            return false;

        exit_hook = hook;

        return true;
    }

    void invoke_exit_hook(arch::syscall_return& image, i64 result) {
        if (exit_hook != nullptr)
            exit_hook(image, result);
    }
} // namespace astia::sys
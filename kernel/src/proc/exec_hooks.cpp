#include "exec_hooks.hpp"

namespace astia::proc {
    namespace {
        exec_hooks g_hooks{};
    } // namespace

    bool register_exec_hooks(const exec_hooks& hooks) {
        if (hooks.interpreter == nullptr || hooks.release_bindings == nullptr ||
            hooks.adopt_bindings == nullptr)
            return false;

        g_hooks = hooks;

        return true;
    }

    core::task<interpreter_info> load_interpreter(process& target, const char* path) {
        if (g_hooks.interpreter == nullptr)
            co_return error_t::enosys;

        auto result = co_await g_hooks.interpreter(target, path);
        if (!result)
            co_return result.error;

        co_return result.value;
    }

    void release_exec_bindings(process& target) {
        if (g_hooks.release_bindings != nullptr)
            g_hooks.release_bindings(target);
    }

    bool adopt_exec_bindings(process& target, process& staging) {
        if (g_hooks.adopt_bindings == nullptr)
            return true;

        return g_hooks.adopt_bindings(target, staging);
    }
} // namespace astia::proc

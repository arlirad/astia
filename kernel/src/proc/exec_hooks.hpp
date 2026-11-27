#pragma once

#include <astia/common.hpp>
#include <astia/core/task.hpp>
#include <astia/optional.hpp>

namespace astia::proc {
    class process;

    /// What the interpreter loader reports back to exec.
    struct interpreter_info {
        u64 base  = 0; ///< AT_BASE
        u64 entry = 0; ///< where exec enters
    };

    /// The pieces of exec that belong to the dynamic-linking subsystem.
    /// Kept as registered callbacks so `proc` and `dl` stay acyclic: `dl`
    /// depends on `proc`, never the other way round.
    struct exec_hooks {
        /// Resolve `path` and map it into `target` as its interpreter.
        core::task<interpreter_info> (*interpreter)(process&    target,
                                                    const char* path) = nullptr;
        /// Drop the target's old library bindings (before its address space
        /// is replaced).
        void (*release_bindings)(process& target) = nullptr;
        /// Move the staging process's bindings onto the process that
        /// replaces it.  Returns false if the handoff failed and the
        /// staging bindings were released.
        bool (*adopt_bindings)(process& target, process& staging) = nullptr;
    };

    bool register_exec_hooks(const exec_hooks& hooks);

    core::task<interpreter_info> load_interpreter(process& target, const char* path);
    void                         release_exec_bindings(process& target);
    bool                         adopt_exec_bindings(process& target, process& staging);
} // namespace astia::proc

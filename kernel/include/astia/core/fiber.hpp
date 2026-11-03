#pragma once

#include <astia/arch/context.hpp>
#include <astia/common.hpp>

namespace astia::core {

    /// Stackful kernel fiber. A fiber may yield only on the CPU that owns it;
    /// resume/yield are deliberately explicit until scheduler integration is
    /// added.
    class fiber {
        public:
        using entry_fn = void (*)(void*);

        fiber(entry_fn entry, void* argument) : entry_(entry), argument_(argument) {}
        fiber(const fiber&)            = delete;
        fiber& operator=(const fiber&) = delete;
        ~fiber();

        bool start();
        bool resume();
        bool done() const { return finished_; }

        /// Yield from the currently running fiber back to its resumer.
        static bool yield();

        private:
        static void trampoline();
        static constexpr usize stack_bytes = 16 * 1024;

        entry_fn        entry_     = nullptr;
        void*           argument_  = nullptr;
        arch::context*  context_   = nullptr;
        arch::context*  caller_    = nullptr;
        void*           stack_     = nullptr;
        u32             owner_cpu_ = 0;
        bool            started_   = false;
        bool            finished_  = false;
    };

} // namespace astia::core

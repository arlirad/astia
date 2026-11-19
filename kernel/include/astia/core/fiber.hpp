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

        fiber(entry_fn entry, void* argument) : _entry(entry), _argument(argument) {}
        fiber(const fiber&)            = delete;
        fiber& operator=(const fiber&) = delete;
        ~fiber();

        bool start();
        bool resume();
        bool done() const {
            return _finished;
        }

        /// Yield from the currently running fiber back to its resumer.
        static bool yield();

        private:
        static void            trampoline();
        static constexpr usize stack_bytes = 16 * 1024;

        entry_fn       _entry     = nullptr;
        void*          _argument  = nullptr;
        arch::context* _context   = nullptr;
        arch::context* _caller    = nullptr;
        void*          _stack     = nullptr;
        u32            _owner_cpu = 0;
        bool           _started   = false;
        bool           _finished  = false;
    };

} // namespace astia::core

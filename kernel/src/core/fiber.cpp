#include <astia/arch/cpu.hpp>
#include <astia/core/fiber.hpp>
#include <astia/mm/heap.hpp>

namespace astia::core {
    namespace {
        fiber* active[256]{};
    }

    fiber::~fiber() {
        if (_context != nullptr)
            arch::context_destroy(_context);
        if (_caller != nullptr)
            arch::context_destroy(_caller);
        if (_stack != nullptr)
            mm::heap::free(_stack);
    }

    bool fiber::start() {
        if (_started || _entry == nullptr)
            return false;
        const u32 cpu = arch::cpu::current_index();
        if (cpu >= 256)
            return false;
        auto stack = mm::heap::alloc(stack_bytes);
        if (!stack)
            return false;
        _context = arch::context_create();
        _caller  = arch::context_create();
        if (!_context || !_caller) {
            if (_context)
                arch::context_destroy(_context);
            if (_caller)
                arch::context_destroy(_caller);
            mm::heap::free(stack.value);
            _context = _caller = nullptr;
            return false;
        }
        _stack     = stack.value;
        _owner_cpu = cpu;
        const u64 top = reinterpret_cast<u64>(_stack) + stack_bytes - 16;
        if (!arch::context_prepare_kernel(_context, top,
                                          reinterpret_cast<u64>(&trampoline)))
            return false;
        _started = true;
        return resume();
    }

    bool fiber::resume() {
        if (!_started || _finished || arch::cpu::current_index() != _owner_cpu ||
            active[_owner_cpu] != nullptr)
            return false;
        active[_owner_cpu] = this;
        arch::context_switch(_caller, _context);
        active[_owner_cpu] = nullptr;
        return true;
    }

    bool fiber::yield() {
        const u32 cpu = arch::cpu::current_index();
        if (cpu >= 256 || active[cpu] == nullptr)
            return false;
        fiber* current = active[cpu];
        active[cpu]     = nullptr;
        arch::context_switch(current->_context, current->_caller);
        return true;
    }

    void fiber::trampoline() {
        const u32 cpu = arch::cpu::current_index();
        fiber* current = cpu < 256 ? active[cpu] : nullptr;
        if (current != nullptr && current->_entry != nullptr)
            current->_entry(current->_argument);
        if (current != nullptr) {
            current->_finished = true;
            active[cpu]        = nullptr;
            arch::context_switch(current->_context, current->_caller);
        }
        __builtin_trap();
    }
} // namespace astia::core

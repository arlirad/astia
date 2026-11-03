#include <astia/arch/cpu.hpp>
#include <astia/core/fiber.hpp>
#include <astia/mm/heap.hpp>

namespace astia::core {
    namespace {
        fiber* active[256]{};
    }

    fiber::~fiber() {
        if (context_ != nullptr)
            arch::context_destroy(context_);
        if (caller_ != nullptr)
            arch::context_destroy(caller_);
        if (stack_ != nullptr)
            mm::heap::free(stack_);
    }

    bool fiber::start() {
        if (started_ || entry_ == nullptr)
            return false;
        const u32 cpu = arch::cpu::current_index();
        if (cpu >= 256)
            return false;
        auto stack = mm::heap::alloc(stack_bytes);
        if (!stack)
            return false;
        context_ = arch::context_create();
        caller_  = arch::context_create();
        if (!context_ || !caller_) {
            if (context_)
                arch::context_destroy(context_);
            if (caller_)
                arch::context_destroy(caller_);
            mm::heap::free(stack.value);
            context_ = caller_ = nullptr;
            return false;
        }
        stack_     = stack.value;
        owner_cpu_ = cpu;
        const u64 top = reinterpret_cast<u64>(stack_) + stack_bytes - 16;
        if (!arch::context_prepare_kernel(context_, top,
                                          reinterpret_cast<u64>(&trampoline)))
            return false;
        started_ = true;
        return resume();
    }

    bool fiber::resume() {
        if (!started_ || finished_ || arch::cpu::current_index() != owner_cpu_ ||
            active[owner_cpu_] != nullptr)
            return false;
        active[owner_cpu_] = this;
        arch::context_switch(caller_, context_);
        active[owner_cpu_] = nullptr;
        return true;
    }

    bool fiber::yield() {
        const u32 cpu = arch::cpu::current_index();
        if (cpu >= 256 || active[cpu] == nullptr)
            return false;
        fiber* current = active[cpu];
        active[cpu]     = nullptr;
        arch::context_switch(current->context_, current->caller_);
        return true;
    }

    void fiber::trampoline() {
        const u32 cpu = arch::cpu::current_index();
        fiber* current = cpu < 256 ? active[cpu] : nullptr;
        if (current != nullptr && current->entry_ != nullptr)
            current->entry_(current->argument_);
        if (current != nullptr) {
            current->finished_ = true;
            active[cpu]        = nullptr;
            arch::context_switch(current->context_, current->caller_);
        }
        __builtin_trap();
    }
} // namespace astia::core

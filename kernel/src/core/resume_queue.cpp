#include <astia/arch/cpu.hpp>
#include <astia/core/resume_queue.hpp>
#include "../proc/scheduler.hpp"

namespace astia::core {
    namespace {
        constexpr u32 max_cpus = proc::scheduler::max_cpu_count;
        mpmc_queue<std::coroutine_handle<>, 256> queues[max_cpus];
    }

    bool schedule_resume(std::coroutine_handle<> continuation) {
        if (!continuation)
            return false;
        const u32 cpu = arch::cpu::current_index();
        return cpu < max_cpus && queues[cpu].try_push(continuation);
    }

    u32 drain_resumes(u32 budget) {
        const u32 cpu = arch::cpu::current_index();
        if (cpu >= max_cpus)
            return 0;
        u32 count = 0;
        std::coroutine_handle<> continuation;
        while (count < budget && queues[cpu].try_pop(continuation)) {
            continuation.resume();
            ++count;
        }
        return count;
    }
} // namespace astia::core

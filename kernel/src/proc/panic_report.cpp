#include <astia/dbg/panic_report.hpp>
#include <astia/log.hpp>

#include "proc.hpp"

namespace astia::dbg {
    namespace {
        void log_sink(void*, const char* text, u32 length) {
            char      line[512];
            const u32 count = length < sizeof line - 2 ? length : sizeof line - 2;

            for (u32 i = 0; i < count; ++i) {
                line[i] = text[i];
            }

            line[count]     = '\n';
            line[count + 1] = '\0';

            log::detail::emit(log::level::panic, line, count + 1);
        }
    } // namespace

    void report_current_panic() {
        panic_process process{};

        proc::panic_snapshot(process);

        format_panic_context(process, log_sink, nullptr);
        arch::panic_report_registers(log_sink, nullptr, nullptr);
    }

    void report_panic(const panic_process& process, const char* reason,
                      const void* frame) {
        if (reason != nullptr)
            log::root::print(log::level::panic, "kpanic: %s", reason);

        format_panic_context(process, log_sink, nullptr);
        arch::panic_report_registers(log_sink, nullptr, frame);
    }
} // namespace astia::dbg

namespace astia::proc {
    namespace {
        const char* thread_state_name(thread_state state) {
            switch (state) {
            case thread_state::fresh:
                return "fresh";
            case thread_state::runnable:
                return "runnable";
            case thread_state::running:
                return "running";
            case thread_state::sleeping:
                return "sleeping";
            case thread_state::blocked:
                return "blocked";
            case thread_state::dead:
                return "dead";
            }

            return "unknown";
        }
    } // namespace

    void panic_snapshot(dbg::panic_process& out) {
        out               = {};
        out.arch          = arch::panic_arch_name();
        out.cpu           = arch::cpu::current_index();
        out.address_space = arch::panic_address_space_root();

        process* value = current_process();
        if (value != nullptr)
            value->panic_snapshot(out);

        thread* value_thread = current_thread();
        if (value_thread != nullptr) {
            out.thread_available  = true;
            out.tid               = value_thread->tid();
            out.thread_state      = thread_state_name(value_thread->state());
            out.kstack_top        = value_thread->kernel_stack_top();
            out.kstack_size       = value_thread->kernel_stack_size();
            out.kstack_high_water = value_thread->kernel_stack_high_water_bytes();
        }
    }
} // namespace astia::proc

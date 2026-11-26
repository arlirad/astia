#include <astia/arch/cpu.hpp>
#include <astia/arch/interrupt_frame.hpp>
#include <astia/arch/interrupts.hpp>
#include <astia/arch/platform.hpp>
#include <astia/atomic.hpp>
#include <astia/common.hpp>
#include <astia/log.hpp>

#include "../arch/x86_64/smp.hpp"
#include "proc.hpp"
#include "scheduler.hpp"
#include "selftest_arch.hpp"

namespace astia::test {

    namespace {

        using logs = astia::log::context<"selftest">;

        u32 ap_work_seen       = 0;
        u32 ap_stress_done     = 0;
        u32 ap_stress_cpu_mask = 0;
        u32 migration_done     = 0;
        u32 migration_cpu_mask = 0;
        u32 vector_test_hits   = 0;

        extern "C" void interrupt_dispatch(arch::interrupt_frame* frame);

        void vector_test_handler(void*) {
            astia::atomic_ref<u32>(vector_test_hits)
                .add_fetch(1, astia::memory_order::relaxed);
        }

        void ap_scheduler_worker(void*) {
            if (arch::cpu::current_index() == 1) {
                astia::atomic_ref<u32>(ap_work_seen)
                    .store(1, astia::memory_order::release);
            }
            proc::current_thread()->exit();
        }

        void ap_stress_worker(void*) {
            const u32 cpu = arch::cpu::current_index();
            astia::atomic_ref<u32>(ap_stress_cpu_mask)
                .or_fetch(1u << cpu, astia::memory_order::release);
            astia::atomic_ref<u32>(ap_stress_done)
                .add_fetch(1, astia::memory_order::release);
            proc::current_thread()->exit();
        }

        void migration_worker(void*) {
            astia::atomic_ref<u32>(migration_cpu_mask)
                .or_fetch(1u << arch::cpu::current_index(), astia::memory_order::release);
            astia::atomic_ref<u32>(migration_done)
                .add_fetch(1, astia::memory_order::release);
            proc::current_thread()->exit();
        }

        bool ap_scheduler_checks() {
            if (arch::platform::online_cpu_count() < 2)
                return true;
            ap_work_seen = 0;
            auto ap_work =
                proc::scheduler::create_kernel_on(1, ap_scheduler_worker, nullptr);
            if (!ap_work || ap_work->detach() != 0) {
                logs::error("smp scheduler: could not queue AP work");
                return false;
            }
            arch::cpu::interrupts();
            for (u32 i = 0; i < 200 && astia::atomic_ref<u32>(ap_work_seen)
                                               .load(astia::memory_order::acquire) == 0;
                 ++i) {
                arch::cpu::halt();
            }
            arch::cpu::nointerrupts();
            if (astia::atomic_ref<u32>(ap_work_seen).load(astia::memory_order::acquire) ==
                0) {
                logs::error("smp scheduler: AP work did not run");
                return false;
            }
            constexpr u32 stress_jobs = 8;
            ap_stress_done            = 0;
            ap_stress_cpu_mask        = 0;
            for (u32 i = 0; i < stress_jobs; ++i) {
                auto job =
                    proc::scheduler::create_kernel_on(1, ap_stress_worker, nullptr);
                if (!job || job->detach() != 0) {
                    logs::error("smp scheduler: stress queue failed at %u", i);
                    return false;
                }
            }
            arch::cpu::interrupts();
            for (u32 i = 0;
                 i < 400 && astia::atomic_ref<u32>(ap_stress_done)
                                    .load(astia::memory_order::acquire) < stress_jobs;
                 ++i) {
                arch::cpu::halt();
            }
            arch::cpu::nointerrupts();
            const u32 done =
                astia::atomic_ref<u32>(ap_stress_done).load(astia::memory_order::acquire);
            const u32 mask = astia::atomic_ref<u32>(ap_stress_cpu_mask)
                                 .load(astia::memory_order::acquire);
            if (done != stress_jobs || mask != (1u << 1)) {
                logs::error("smp scheduler: stress done=%u mask=%x", done, mask);
                return false;
            }
            constexpr u32 migration_jobs = 8;
            migration_done               = 0;
            migration_cpu_mask           = 0;
            for (u32 i = 0; i < migration_jobs; ++i) {
                auto job = proc::scheduler::create_kernel(
                    migration_worker, nullptr, proc::scheduling_class::normal, 0, 0,
                    proc::scheduler::thread_options::migratable);
                if (!job || job->detach() != 0) {
                    logs::error("smp scheduler: migration queue failed at %u", i);
                    return false;
                }
            }
            arch::cpu::request_reschedule();
            arch::cpu::interrupts();
            for (u32 i = 0;
                 i < 400 && astia::atomic_ref<u32>(migration_done)
                                    .load(astia::memory_order::acquire) < migration_jobs;
                 ++i) {
                arch::cpu::halt();
            }
            arch::cpu::nointerrupts();
            const u32 migrated_done =
                astia::atomic_ref<u32>(migration_done).load(astia::memory_order::acquire);
            const u32 migrated_mask = astia::atomic_ref<u32>(migration_cpu_mask)
                                          .load(astia::memory_order::acquire);
            if (migrated_done != migration_jobs || (migrated_mask & (1u << 1)) == 0) {
                logs::error("smp scheduler: migration done=%u mask=%x", migrated_done,
                            migrated_mask);
                return false;
            }
            logs::info("smp scheduler: migration ok");
            logs::info(
                "smp scheduler: AP work ok; stress=%u cpu-mask=%x migration=%u mask=%x",
                done, mask, migrated_done, migrated_mask);
            return true;
        }

        bool interrupt_checks() {
            // Gate observability.
            arch::cpu::nointerrupts();
            if (arch::cpu::check_interrupts()) {
                logs::error("interrupts fail (gate)");
                return false;
            }

            // Software dispatch through the real IDT.
            asm volatile("int $3");
            if (arch::breakpoint_hits() != 1) {
                logs::error("interrupts fail (int3)");
                return false;
            }

            // Scoped interrupt masking and restoration.
            arch::cpu::interrupts();
            {
                arch::interrupt_guard guard;
                if (arch::cpu::check_interrupts()) {
                    logs::error("interrupts fail (guard mask)");
                    return false;
                }
            }
            if (!arch::cpu::check_interrupts()) {
                logs::error("interrupts fail (guard restore)");
                return false;
            }

            // Hardware dispatch: wait for a real LAPIC/PIT tick.
            const u64 tick_start = arch::timer_ticks();
            for (u32 i = 0; i < 100 && arch::timer_ticks() == tick_start; ++i) {
                arch::cpu::halt();
            }
            const bool ticked = arch::timer_ticks() != tick_start;
            arch::cpu::nointerrupts();
            if (!ticked) {
                logs::error("interrupts fail (tick)");
                return false;
            }

            if (arch::current_timer_source() == arch::timer_source::lapic) {
                arch::interrupt_vector route{};
                vector_test_hits = 0;
                if (!arch::register_interrupt_vector(vector_test_handler, nullptr,
                                                     route)) {
                    logs::error("interrupt vectors fail (register)");
                    return false;
                }
                arch::msi_message message{};
                if (!arch::interrupt_vector_message(route, message) ||
                    message.address_high != 0 || message.data != route.vector) {
                    logs::error("interrupt vectors fail (message)");
                    return false;
                }
                if (!arch::unmask_interrupt_vector(route)) {
                    logs::error("interrupt vectors fail (unmask)");
                    return false;
                }
                arch::interrupt_frame frame{};
                frame.vector = route.vector;
                interrupt_dispatch(&frame);
                const u32 first_hits = astia::atomic_ref<u32>(vector_test_hits)
                                           .load(astia::memory_order::relaxed);
                const bool masked = arch::mask_interrupt_vector(route);
                if (first_hits != 1 || !masked) {
                    logs::error("interrupt vectors fail (dispatch/mask) vec=%u owner=%u "
                                "hits=%u masked=%u",
                                route.vector, route.owner, first_hits, masked ? 1u : 0u);
                    return false;
                }
                interrupt_dispatch(&frame);
                if (astia::atomic_ref<u32>(vector_test_hits)
                            .load(astia::memory_order::relaxed) != 1 ||
                    !arch::unmask_interrupt_vector(route)) {
                    logs::error("interrupt vectors fail (masked dispatch/unmask)");
                    return false;
                }
                interrupt_dispatch(&frame);
                if (astia::atomic_ref<u32>(vector_test_hits)
                            .load(astia::memory_order::relaxed) != 2 ||
                    !arch::unregister_interrupt_vector(route) ||
                    arch::mask_interrupt_vector(route) ||
                    arch::unmask_interrupt_vector(route)) {
                    logs::error("interrupt vectors fail (lifecycle)");
                    return false;
                }
                arch::interrupt_vector reused{};
                if (!arch::register_interrupt_vector(vector_test_handler, nullptr,
                                                     reused) ||
                    reused.vector != route.vector || reused.owner == route.owner ||
                    !arch::unregister_interrupt_vector(reused)) {
                    logs::error("interrupt vectors fail (reuse)");
                    return false;
                }
                logs::info("interrupt vectors ok");
            }

            logs::info("interrupts ok (%s)",
                       arch::current_timer_source() == arch::timer_source::lapic ? "lapic"
                                                                                 : "pit");
            return true;
        }

    } // namespace

    bool run_arch_checks() {
        if (!ap_scheduler_checks()) {
            return false;
        }
        return interrupt_checks();
    }

} // namespace astia::test

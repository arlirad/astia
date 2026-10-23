#include "selftest.hpp"

#include "../dev/chr/tty/uart16550.hpp"
#include "aio.hpp"
#include "proc.hpp"
#include "scheduler.hpp"
#include "syscall.hpp"
#include <astia/api/aio.h>
#include <astia/api/errno.h>
#include <astia/api/syscalls.h>
#include <astia/arch/cpu.hpp>
#include <astia/arch/interrupts.hpp>
#include <astia/common.hpp>
#include <astia/fs.hpp>
#include <astia/log.hpp>
#include "../arch/x86_64/smp.hpp"

namespace astia::test {

    namespace {

        /// This suite's log source.
        using logs = astia::log::context<"selftest">;

        volatile u32 scheduler_counts[2]{};
        volatile u32 ap_work_seen = 0;
        volatile u32 ap_stress_done = 0;
        volatile u32 ap_stress_cpu_mask = 0;
        volatile u32 migration_done = 0;
        volatile u32 migration_cpu_mask = 0;
        volatile u32 preempt_started   = 0;
        volatile u32 preempt_main_seen = 0;
        volatile u32 preempt_done      = 0;

        void scheduler_worker(void* argument) {
            auto* count = static_cast<volatile u32*>(argument);
            for (u32 i = 0; i < 3; ++i) {
                __atomic_add_fetch(count, 1, __ATOMIC_RELAXED);
                proc::scheduler::yield();
            }
        }

        void ap_scheduler_worker(void*) {
            if (arch::cpu::current_index() == 1) {
                __atomic_store_n(&ap_work_seen, 1, __ATOMIC_RELEASE);
            }
            proc::current_thread()->exit();
        }

        void ap_stress_worker(void*) {
            const u32 cpu = arch::cpu::current_index();
            __atomic_or_fetch(&ap_stress_cpu_mask, 1u << cpu, __ATOMIC_RELEASE);
            __atomic_add_fetch(&ap_stress_done, 1, __ATOMIC_RELEASE);
            proc::current_thread()->exit();
        }

        void migration_worker(void*) {
            __atomic_or_fetch(&migration_cpu_mask, 1u << arch::cpu::current_index(),
                              __ATOMIC_RELEASE);
            __atomic_add_fetch(&migration_done, 1, __ATOMIC_RELEASE);
            proc::current_thread()->exit();
        }

        bool ap_scheduler_checks() {
            if (arch::x86_64::smp::online_cpus() < 2) return true;
            ap_work_seen = 0;
            auto ap_work = proc::scheduler::create_kernel_on(1, ap_scheduler_worker, nullptr);
            if (!ap_work || ap_work->detach() != 0) {
                logs::error("smp scheduler: could not queue AP work");
                return false;
            }
            arch::cpu::interrupts();
            for (u32 i = 0; i < 200 &&
                            __atomic_load_n(&ap_work_seen, __ATOMIC_ACQUIRE) == 0;
                 ++i) {
                arch::cpu::halt();
            }
            arch::cpu::nointerrupts();
            if (__atomic_load_n(&ap_work_seen, __ATOMIC_ACQUIRE) == 0) {
                logs::error("smp scheduler: AP work did not run");
                return false;
            }
            constexpr u32 stress_jobs = 8;
            ap_stress_done = 0;
            ap_stress_cpu_mask = 0;
            for (u32 i = 0; i < stress_jobs; ++i) {
                auto job = proc::scheduler::create_kernel_on(1, ap_stress_worker, nullptr);
                if (!job || job->detach() != 0) {
                    logs::error("smp scheduler: stress queue failed at %u", i);
                    return false;
                }
            }
            arch::cpu::interrupts();
            for (u32 i = 0;
                 i < 400 && __atomic_load_n(&ap_stress_done, __ATOMIC_ACQUIRE) < stress_jobs;
                 ++i) {
                arch::cpu::halt();
            }
            arch::cpu::nointerrupts();
            const u32 done = __atomic_load_n(&ap_stress_done, __ATOMIC_ACQUIRE);
            const u32 mask = __atomic_load_n(&ap_stress_cpu_mask, __ATOMIC_ACQUIRE);
            if (done != stress_jobs || mask != (1u << 1)) {
                logs::error("smp scheduler: stress done=%u mask=%x", done, mask);
                return false;
            }
            constexpr u32 migration_jobs = 8;
            migration_done = 0;
            migration_cpu_mask = 0;
            for (u32 i = 0; i < migration_jobs; ++i) {
                auto job = proc::scheduler::create_kernel_on(0, migration_worker, nullptr);
                if (!job || job->detach() != 0 || !proc::scheduler::migrate(job.get(), 1)) {
                    logs::error("smp scheduler: migration queue failed at %u", i);
                    return false;
                }
            }
            arch::cpu::interrupts();
            for (u32 i = 0;
                 i < 400 && __atomic_load_n(&migration_done, __ATOMIC_ACQUIRE) < migration_jobs;
                 ++i) {
                arch::cpu::halt();
            }
            arch::cpu::nointerrupts();
            const u32 migrated_done = __atomic_load_n(&migration_done, __ATOMIC_ACQUIRE);
            const u32 migrated_mask = __atomic_load_n(&migration_cpu_mask, __ATOMIC_ACQUIRE);
            if (migrated_done != migration_jobs || migrated_mask != (1u << 1)) {
                logs::error("smp scheduler: migration done=%u mask=%x", migrated_done,
                            migrated_mask);
                return false;
            }
            logs::info("smp scheduler: AP work ok; stress=%u cpu-mask=%x migration=%u mask=%x",
                       done, mask, migrated_done, migrated_mask);
            return true;
        }

        bool scheduler_checks() {
            const bool gate_open = arch::cpu::check_interrupts();
            arch::cpu::interrupts();
            scheduler_counts[0] = 0;
            scheduler_counts[1] = 0;
            if (!proc::scheduler::spawn_kernel(scheduler_worker,
                                               const_cast<u32*>(&scheduler_counts[0])) ||
                !proc::scheduler::spawn_kernel(scheduler_worker,
                                               const_cast<u32*>(&scheduler_counts[1]))) {
                if (!gate_open) {
                    arch::cpu::nointerrupts();
                }
                logs::error("scheduler: could not spawn kernel threads");
                return false;
            }
            for (u32 i = 0;
                 i < 16 && (scheduler_counts[0] != 3 || scheduler_counts[1] != 3); ++i) {
                proc::scheduler::yield();
            }
            const u32 first  = __atomic_load_n(&scheduler_counts[0], __ATOMIC_RELAXED);
            const u32 second = __atomic_load_n(&scheduler_counts[1], __ATOMIC_RELAXED);
            if (!gate_open) {
                arch::cpu::nointerrupts();
            }
            if (first != 3 || second != 3) {
                logs::error("scheduler: counts=%u,%u", first, second);
                return false;
            }
            logs::info("scheduler ok");
            return true;
        }

        void preempt_worker(void*) {
            __atomic_store_n(&preempt_started, 1, __ATOMIC_RELEASE);
            while (__atomic_load_n(&preempt_main_seen, __ATOMIC_ACQUIRE) == 0) {
                // Deliberately no yield: only timer preemption can return
                // control to the bootstrap thread.
            }
            __atomic_store_n(&preempt_done, 1, __ATOMIC_RELEASE);
        }

        void join_worker(void*) {
            proc::current_thread()->exit(reinterpret_cast<void*>(0x42));
        }

        bool join_checks() {
            if (proc::current_thread()->join() != -EDEADLK) {
                logs::error("join: self-join was not rejected");
                return false;
            }
            auto target = proc::scheduler::create_kernel(join_worker, nullptr);
            if (!target) {
                logs::error("join: could not spawn kernel thread");
                return false;
            }
            void*      result = nullptr;
            const long rc     = target->join(&result);
            if (rc != 0 || result != reinterpret_cast<void*>(0x42)) {
                logs::error("join: failed rc=%ld result=%p", rc, result);
                return false;
            }
            if (target->join() != -EINVAL) {
                logs::error("join: double join was not rejected");
                return false;
            }

            auto detached = proc::scheduler::create_kernel(join_worker, nullptr);
            if (!detached || detached->detach() != 0 || detached->join() != -EINVAL) {
                logs::error("join: detached join was not rejected");
                return false;
            }
            proc::scheduler::yield();
            logs::info("join ok");
            return true;
        }

        bool preemption_checks() {
            preempt_started   = 0;
            preempt_main_seen = 0;
            preempt_done      = 0;
            if (!proc::scheduler::spawn_kernel(preempt_worker, nullptr)) {
                logs::error("preemption: could not spawn kernel thread");
                return false;
            }

            arch::cpu::interrupts();
            for (u32 i = 0;
                 i < 200 && __atomic_load_n(&preempt_started, __ATOMIC_ACQUIRE) == 0;
                 ++i) {
                arch::cpu::halt();
            }
            if (__atomic_load_n(&preempt_started, __ATOMIC_ACQUIRE) == 0) {
                arch::cpu::nointerrupts();
                logs::error("preemption: did not switch to worker");
                return false;
            }
            __atomic_store_n(&preempt_main_seen, 1, __ATOMIC_RELEASE);
            for (u32 i = 0;
                 i < 200 && __atomic_load_n(&preempt_done, __ATOMIC_ACQUIRE) == 0; ++i) {
                arch::cpu::halt();
            }
            arch::cpu::nointerrupts();
            if (__atomic_load_n(&preempt_done, __ATOMIC_ACQUIRE) == 0) {
                logs::error("preemption: worker did not finish");
                return false;
            }
            logs::info("preemption ok");
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

            logs::info("interrupts ok (%s)",
                       arch::current_timer_source() == arch::timer_source::lapic ? "lapic"
                                                                                 : "pit");
            return true;
        }

        // SYS_CLOCK_GETTIME, dispatched through the registered table (the
        // sys_time init step is what put the handler there). The tick is
        // the resolution, so samples only have to be in range and must not
        // run backwards.
        bool clock_checks() {
            // The table's door takes what the entry would have handed it:
            // the call number, the six registers, and the image it packed
            // them into. clock_gettime reads registers and never looks at
            // the image, but the image is still the real one a trap for
            // this call would have made.
            auto read = [](i64 id, astia_timespec* out) -> long {
                arch::syscall_frame frame{};
                frame.nr = static_cast<u64>(sys::syscall_id::clock_gettime);
                frame.a0 = static_cast<u64>(id);
                frame.a1 = reinterpret_cast<u64>(out);
                return static_cast<long>(sys::dispatch(
                    sys::syscall_id::clock_gettime, static_cast<i64>(frame.a0),
                    static_cast<i64>(frame.a1), 0, 0, 0, 0, frame));
            };

            astia_timespec first{};
            astia_timespec second{};
            astia_timespec wall{};

            if (read(CLOCK_MONOTONIC, &first) != 0 || first.tv_nsec < 0 ||
                first.tv_nsec >= 1000000000) {
                logs::error("clock fail (monotonic)");
                return false;
            }
            if (read(CLOCK_REALTIME, &wall) != 0 || wall.tv_sec < 1000000000) {
                // Wall time is uptime anchored at a fixed epoch: it must be
                // far past 2001, unlike the uptime reading next to it.
                logs::error("clock fail (realtime)");
                return false;
            }
            if (read(CLOCK_MONOTONIC, &second) != 0 || second.tv_sec < first.tv_sec ||
                (second.tv_sec == first.tv_sec && second.tv_nsec < first.tv_nsec)) {
                logs::error("clock fail (regression)");
                return false;
            }
            if (read(99, &first) != -EINVAL) {
                logs::error("clock fail (bad clock id)");
                return false;
            }

            logs::info("clock ok");
            return true;
        }

        // A VFS node with a real size, so STATX has a success path whether
        // or not an initrd is mounted on this boot.
        struct sized_probe : fs::file {
            long size() const override {
                return 4096;
            }
        };
        sized_probe probe_node{};

        // OP_STATX on a ring: the success path (both address forms) and the
        // errors the VFS can actually produce for them.
        bool statx_checks() {
            static const char probe_path[]   = "/dev/statx_probe";
            static const char missing_path[] = "/dev/astia_no_such_node";

            if (fs::register_device(probe_path,
                                    mm::shptr<fs::file>::borrowed(&probe_node)) < 0) {
                logs::error("statx fail (probe node)");
                return false;
            }

            astia_rings* ring = nullptr;
            if (sys::io_setup(8, 0, &ring) < 0 || ring == nullptr) {
                logs::error("statx fail (ring)");
                return false;
            }

            auto statx = [&](const char* path, i32 fd, astia_stat* out) -> long {
                astia_sqe* s = astia_sqe_slot(ring);
                if (s == nullptr) {
                    return -EBUSY;
                }
                *s           = astia_sqe{};
                s->op        = OP_STATX;
                s->fd        = fd;
                s->address   = path == nullptr ? 0 : reinterpret_cast<u64>(path);
                s->offset    = reinterpret_cast<u64>(out);
                s->length    = static_cast<u32>(sizeof(astia_stat));
                s->user_data = 0xC0DE57;
                astia_sq_publish(ring, 1);
                if (sys::io_enter(ring, 1, 1, ENTER_GETEVENTS, 100) < 0) {
                    return -EIO;
                }
                astia_cqe* c = astia_cq_peek(ring);
                if (c == nullptr || c->user_data != 0xC0DE57) {
                    return -EIO;
                }
                const long result = c->result;
                astia_cq_consume(ring, 1);
                return result;
            };

            astia_stat st{};
            if (statx(probe_path, -1, &st) != 0 || st.size != 4096 || st.blocks != 8) {
                logs::error("statx fail (path form)");
                return false;
            }
            if (statx(missing_path, -1, &st) != -ENOENT) {
                logs::error("statx fail (missing path)");
                return false;
            }
            if (statx(nullptr, -1, &st) != -EBADF) {
                logs::error("statx fail (fstat without descriptor)");
                return false;
            }

            (void) sys::io_destroy(ring);
            logs::info("statx ok");
            return true;
        }

    } // namespace

    bool run() {
        if (!scheduler_checks()) {
            return false;
        }
        if (!ap_scheduler_checks()) {
            return false;
        }
        if (!join_checks()) {
            return false;
        }
        if (!interrupt_checks()) {
            return false;
        }
        if (!preemption_checks()) {
            return false;
        }
        if (!clock_checks()) {
            return false;
        }
        if (!statx_checks()) {
            return false;
        }

        // The UART test owns its gate window and exercises the VFS/open
        // file/ring path in loopback mode.
        arch::cpu::interrupts();
        const bool ring_ok = dev::uart16550_ring_selftest();
        arch::cpu::nointerrupts();
        if (!ring_ok) {
            logs::error("uart16550 ring fail");
            return false;
        }
        logs::info("uart16550 ring ok");
        return true;
    }

} // namespace astia::test

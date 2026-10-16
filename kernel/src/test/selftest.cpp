#include <astia/api/aio.h>
#include <astia/api/errno.h>
#include <astia/api/syscalls.h>
#include <astia/arch/cpu.hpp>
#include <astia/arch/interrupts.hpp>
#include <astia/common.hpp>
#include <astia/dev/uart16550.hpp>
#include <astia/fs/fs.hpp>
#include <astia/log/log.hpp>
#include <astia/proc/proc.hpp>
#include <astia/proc/scheduler.hpp>
#include <astia/sys/io.hpp>
#include <astia/sys/syscall.hpp>
#include <astia/test/selftest.hpp>

namespace astia::test {

    namespace {

        volatile u32 scheduler_counts[2]{};
        volatile u32 preempt_started   = 0;
        volatile u32 preempt_main_seen = 0;
        volatile u32 preempt_done      = 0;

        auto scheduler_worker(void* argument) -> void {
            auto* count = static_cast<volatile u32*>(argument);
            for (u32 i = 0; i < 3; ++i) {
                __atomic_add_fetch(count, 1, __ATOMIC_RELAXED);
                proc::scheduler::yield();
            }
        }

        auto scheduler_checks() -> bool {
            scheduler_counts[0] = 0;
            scheduler_counts[1] = 0;
            if (!proc::scheduler::spawn_kernel(scheduler_worker,
                                               const_cast<u32*>(&scheduler_counts[0])) ||
                !proc::scheduler::spawn_kernel(scheduler_worker,
                                               const_cast<u32*>(&scheduler_counts[1]))) {
                log::error("scheduler selftest could not spawn kernel threads");
                return false;
            }
            for (u32 i = 0;
                 i < 16 && (scheduler_counts[0] != 3 || scheduler_counts[1] != 3); ++i) {
                proc::scheduler::yield();
            }
            const u32 first  = __atomic_load_n(&scheduler_counts[0], __ATOMIC_RELAXED);
            const u32 second = __atomic_load_n(&scheduler_counts[1], __ATOMIC_RELAXED);
            if (first != 3 || second != 3) {
                log::error("scheduler selftest counts=%u,%u", first, second);
                return false;
            }
            log::info("scheduler selftest ok");
            return true;
        }

        auto preempt_worker(void*) -> void {
            __atomic_store_n(&preempt_started, 1, __ATOMIC_RELEASE);
            while (__atomic_load_n(&preempt_main_seen, __ATOMIC_ACQUIRE) == 0) {
                // Deliberately no yield: only timer preemption can return
                // control to the bootstrap thread.
            }
            __atomic_store_n(&preempt_done, 1, __ATOMIC_RELEASE);
        }

        auto join_worker(void*) -> void {
            proc::current_thread()->exit(reinterpret_cast<void*>(0x42));
        }

        auto join_checks() -> bool {
            if (proc::current_thread()->join() != -EDEADLK) {
                log::error("join selftest self-join was not rejected");
                return false;
            }
            auto target = proc::scheduler::create_kernel(join_worker, nullptr);
            if (!target) {
                log::error("join selftest could not spawn kernel thread");
                return false;
            }
            void*      result = nullptr;
            const long rc     = target->join(&result);
            if (rc != 0 || result != reinterpret_cast<void*>(0x42)) {
                log::error("join selftest failed rc=%ld result=%p", rc, result);
                return false;
            }
            if (target->join() != -EINVAL) {
                log::error("join selftest double join was not rejected");
                return false;
            }

            auto detached = proc::scheduler::create_kernel(join_worker, nullptr);
            if (!detached || detached->detach() != 0 || detached->join() != -EINVAL) {
                log::error("join selftest detached join was not rejected");
                return false;
            }
            proc::scheduler::yield();
            log::info("join selftest ok");
            return true;
        }

        auto preemption_checks() -> bool {
            preempt_started   = 0;
            preempt_main_seen = 0;
            preempt_done      = 0;
            if (!proc::scheduler::spawn_kernel(preempt_worker, nullptr)) {
                log::error("preemption selftest could not spawn kernel thread");
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
                log::error("preemption selftest did not switch to worker");
                return false;
            }
            __atomic_store_n(&preempt_main_seen, 1, __ATOMIC_RELEASE);
            for (u32 i = 0;
                 i < 200 && __atomic_load_n(&preempt_done, __ATOMIC_ACQUIRE) == 0; ++i) {
                arch::cpu::halt();
            }
            arch::cpu::nointerrupts();
            if (__atomic_load_n(&preempt_done, __ATOMIC_ACQUIRE) == 0) {
                log::error("preemption selftest worker did not finish");
                return false;
            }
            log::info("preemption selftest ok");
            return true;
        }

        auto interrupt_checks() -> bool {
            // Gate observability.
            arch::cpu::nointerrupts();
            if (arch::cpu::check_interrupts()) {
                log::error("interrupts selftest fail (gate)");
                return false;
            }

            // Software dispatch through the real IDT.
            asm volatile("int $3");
            if (arch::breakpoint_hits() != 1) {
                log::error("interrupts selftest fail (int3)");
                return false;
            }

            // Scoped interrupt masking and restoration.
            arch::cpu::interrupts();
            {
                arch::interrupt_guard guard;
                if (arch::cpu::check_interrupts()) {
                    log::error("interrupts selftest fail (guard mask)");
                    return false;
                }
            }
            if (!arch::cpu::check_interrupts()) {
                log::error("interrupts selftest fail (guard restore)");
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
                log::error("interrupts selftest fail (tick)");
                return false;
            }

            log::info("interrupts ok (%s)",
                      arch::current_timer_source() == arch::timer_source::lapic ? "lapic"
                                                                                : "pit");
            return true;
        }

        // SYS_CLOCK_GETTIME, dispatched through the registered table (the
        // sys_time init step is what put the handler there). The tick is
        // the resolution, so samples only have to be in range and must not
        // run backwards.
        auto clock_checks() -> bool {
            auto read = [](i64 id, astia_timespec* out) -> long {
                return static_cast<long>(
                    sys::dispatch(sys::syscall_id::clock_gettime, id,
                                  reinterpret_cast<i64>(out), 0, 0, 0, 0));
            };

            astia_timespec first {};
            astia_timespec second {};
            astia_timespec wall {};

            if (read(CLOCK_MONOTONIC, &first) != 0 || first.tv_nsec < 0 ||
                first.tv_nsec >= 1000000000) {
                log::error("clock selftest fail (monotonic)");
                return false;
            }
            if (read(CLOCK_REALTIME, &wall) != 0 || wall.tv_sec < 1000000000) {
                // Wall time is uptime anchored at a fixed epoch: it must be
                // far past 2001, unlike the uptime reading next to it.
                log::error("clock selftest fail (realtime)");
                return false;
            }
            if (read(CLOCK_MONOTONIC, &second) != 0 || second.tv_sec < first.tv_sec ||
                (second.tv_sec == first.tv_sec && second.tv_nsec < first.tv_nsec)) {
                log::error("clock selftest fail (regression)");
                return false;
            }
            if (read(99, &first) != -EINVAL) {
                log::error("clock selftest fail (bad clock id)");
                return false;
            }

            log::info("clock ok");
            return true;
        }

        // A VFS node with a real size, so STATX has a success path whether
        // or not an initrd is mounted on this boot.
        struct sized_probe : fs::file {
            auto size() const -> long override {
                return 4096;
            }
        };
        sized_probe probe_node {};

        // OP_STATX on a ring: the success path (both address forms) and the
        // errors the VFS can actually produce for them.
        auto statx_checks() -> bool {
            static const char probe_path[]    = "/dev/statx_probe";
            static const char missing_path[]  = "/dev/astia_no_such_node";

            if (fs::register_device(probe_path,
                                    mm::shptr<fs::file>::borrowed(&probe_node)) < 0) {
                log::error("statx selftest fail (probe node)");
                return false;
            }

            astia_rings* ring = nullptr;
            if (sys::io_setup(8, 0, &ring) < 0 || ring == nullptr) {
                log::error("statx selftest fail (ring)");
                return false;
            }

            auto statx = [&](const char* path, i32 fd, astia_stat* out) -> long {
                astia_sqe* s = astia_sqe_slot(ring);
                if (s == nullptr) {
                    return -EBUSY;
                }
                *s                     = astia_sqe {};
                s->op                  = OP_STATX;
                s->fd                  = fd;
                s->address             = path == nullptr ? 0
                                                         : reinterpret_cast<u64>(path);
                s->offset              = reinterpret_cast<u64>(out);
                s->length              = static_cast<u32>(sizeof(astia_stat));
                s->user_data           = 0xC0DE57;
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

            astia_stat st {};
            if (statx(probe_path, -1, &st) != 0 || st.size != 4096 || st.blocks != 8) {
                log::error("statx selftest fail (path form)");
                return false;
            }
            if (statx(missing_path, -1, &st) != -ENOENT) {
                log::error("statx selftest fail (missing path)");
                return false;
            }
            if (statx(nullptr, -1, &st) != -EBADF) {
                log::error("statx selftest fail (fstat without descriptor)");
                return false;
            }

            (void) sys::io_destroy(ring);
            log::info("statx ok");
            return true;
        }

    } // namespace

    auto run() -> bool {
        if (!scheduler_checks()) {
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
            log::error("uart16550 ring selftest fail");
            return false;
        }
        log::info("uart16550 ring ok");
        return true;
    }

} // namespace astia::test

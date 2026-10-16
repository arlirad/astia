#include "cpu.hpp"
#include "debug_exit.hpp"
#include "log.hpp"
#include "panic.hpp"
#include "proc.hpp"
#include "scheduler.hpp"
#include "selftest.hpp"
#include "uart16550.hpp"
#include <astia/boot.hpp>
#include <astia/common.hpp>
#include <astia/init.hpp>

namespace {

    /// Spawn one userspace image and wait for it out.  `spawned` tells
    /// spawn failure apart from a bad exit status.
    bool run_image(const char* path, char* const argv[], char* const envp[],
                   bool& spawned) {
        spawned      = false;
        auto process = astia::proc::spawnve(path, argv, envp);
        if (!process) {
            return false;
        }
        spawned                    = true;
        const astia::i32 child_pid = static_cast<astia::i32>(process->pid());
        astia::i32       status    = -1;
        const astia::i64 reaped    = astia::proc::waitpid(child_pid, &status);
        astia::log::info("spawn reaped pid=%ld status=%d path=%s",
                         static_cast<long>(reaped), status, path);
        // The shptr releases here, while the frame still unwinds.
        return reaped == child_pid && status == 0;
    }

    void spawn_init_worker(void* opaque) {
        auto* ok = static_cast<volatile astia::u32*>(opaque);
        {
            // Two userspace images, each judged by its exit status: the
            // raw-ABI fixture, then the same tree's libc as a real C
            // program (CRT0, TLS, stdio over the rings).
            char*      init_argv[] = {const_cast<char*>("/sbin/init"),
                                      const_cast<char*>("--flag"), nullptr};
            char*      init_envp[] = {const_cast<char*>("HOME=/"), nullptr};
            char*      libc_argv[] = {const_cast<char*>("/bin/init_libc"), nullptr};
            char*      libc_envp[] = {const_cast<char*>("HOME=/"), nullptr};
            bool       spawned     = false;
            const bool init_ok = run_image("/sbin/init", init_argv, init_envp, spawned);
            if (!spawned) {
                __atomic_store_n(ok, 2u, __ATOMIC_RELEASE);
            }
            else {
                const bool libc_ok =
                    run_image("/bin/init_libc", libc_argv, libc_envp, spawned);
                __atomic_store_n(ok, (init_ok && libc_ok) ? 1u : 3u, __ATOMIC_RELEASE);
            }
            // Process references were released inside run_image before
            // exit(): exit never returns, so an unwound frame (and its
            // shptrs) would otherwise leak.
        }
        astia::proc::current_thread()->exit();
    }

}

namespace {
    [[noreturn]] void halt() {
        for (;;) {
            astia::arch::cpu::halt();
        }
    }

} // namespace

extern "C" void kmain(void* start_info) {
    astia::log::info("kmain seed start_info=%p", start_info);

    astia::init::context ctx{};
    ctx.start_info     = start_info;
    const bool init_ok = astia::init::run(ctx);
    astia::log::info("init result=%s", init_ok ? "ok" : "fail");

    if (!init_ok || !astia::test::run()) {
        astia::core::kpanic("kernel integration selftest failed");
    }

    if (ctx.info != nullptr && ctx.info->module_count != 0) {
        volatile astia::u32 spawn_status = 0;
        auto                worker       = astia::proc::scheduler::create_kernel(
            spawn_init_worker, const_cast<astia::u32*>(&spawn_status));
        // The worker blocks in waitpid until user init exits, and this
        // bootstrap thread blocks in join() until the worker exits;
        // timer preemption drives the whole chain.
        astia::arch::cpu::interrupts();
        if (!worker || worker->join() != 0 ||
            __atomic_load_n(&spawn_status, __ATOMIC_ACQUIRE) != 1) {
            astia::core::kpanic("unable to spawn/reap userspace images");
        }
        astia::arch::cpu::nointerrupts();
        astia::log::info("init lifecycle ok");
    }

    astia::log::info("selftest ok");
    astia::dev::debug_exit(0);
    halt();
}

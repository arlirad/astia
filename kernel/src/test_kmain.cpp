#include <astia/arch/cpu.hpp>
#include <astia/arch/platform.hpp>
#include <astia/boot.hpp>
#include <astia/common.hpp>
#include <astia/fs.hpp>
#include <astia/init.hpp>
#include <astia/log.hpp>
#include <astia/version.hpp>

#include "debug_exit.hpp"
#include "dev/chr/tty/uart16550.hpp"
#include "mod/module_driver.hpp"
#include "panic.hpp"
#include "proc.hpp"
#include "scheduler.hpp"
#include "selftest.hpp"

#ifndef ASTIA_RUN_SELFTEST
#define ASTIA_RUN_SELFTEST 1
#endif

#ifndef ASTIA_RUN_LIFECYCLE
#define ASTIA_RUN_LIFECYCLE 1
#endif

namespace {

    astia::core::task<astia::mm::shptr<astia::proc::process>>
    spawn_image(astia::proc::process* parent, const char* path, char* const argv[],
                char* const envp[]) {
        if (parent == nullptr || path == nullptr)
            co_return astia::error_t::einval;
        auto file = astia::fs::open_path(path);
        if (!file)
            co_return astia::error_t::enoent;
        const long size = file->size();
        if (size <= 0)
            co_return astia::error_t::einval;
        auto result = co_await astia::proc::spawnve(
            parent, file, static_cast<astia::usize>(size), argv, envp);
        if (!result)
            co_return result.error;
        co_return static_cast<astia::mm::shptr<astia::proc::process>&&>(result.value);
    }

    /// Spawn one userspace image and wait for it out.  `spawned` tells
    /// spawn failure apart from a bad exit status.
    astia::core::task<bool> run_image(const char* path, char* const argv[],
                                      char* const envp[], bool& spawned) {
        astia::arch::cpu::interrupts();
        spawned = false;
        auto loaded =
            co_await spawn_image(astia::proc::current_process(), path, argv, envp);
        if (!loaded)
            co_return false;
        auto process =
            static_cast<astia::mm::shptr<astia::proc::process>&&>(loaded.value);
        if (!process)
            co_return false;
        spawned = true;
        const astia::i32 child_pid =
            static_cast<astia::i32>(static_cast<astia::u32>(process->pid()));
        astia::i32       status = -1;
        auto             waited = co_await astia::proc::waitpid(child_pid, &status);
        const astia::i64 reaped = waited ? waited.value : -1;
        // Keep process teardown on this lifecycle stack non-preemptible.
        astia::arch::cpu::nointerrupts();
        astia::log::info("spawn reaped pid=%ld status=%d path=%s",
                         static_cast<long>(reaped), status, path);
        const bool ok = reaped == child_pid && status == 0;
        process       = {};
        co_return ok;
    }

    /// Wait out both userspace images on the seed thread.  PID 1's parent is
    /// still the kernel process; only a coroutine sits above the waits.
    astia::core::task<astia::i64> run_lifecycle(char* const init_argv[],
                                                char* const init_envp[],
                                                char* const libc_argv[],
                                                char* const libc_envp[]) {
        bool spawned = false;
        auto first   = co_await run_image("/sbin/init", init_argv, init_envp, spawned);
        if (!first || !spawned)
            co_return -1;
        bool ok = first.value;
        if (ok) {
            auto second =
                co_await run_image("/bin/init_libc", libc_argv, libc_envp, spawned);
            ok = second && spawned && second.value;
        }
        co_return ok ? 0 : -1;
    }

    /// The whole test-kernel flow, from early init through the userspace
    /// lifecycle.  Early init and the in-kernel selftests run synchronously
    /// before the first co_await; only the lifecycle images suspend.
    astia::core::task<void> kmain(void* start_info) {
        astia::log::info("version=%s", ASTIA_VERSION_STRING);
        astia::log::info("kmain seed start_info=%p", start_info);

        astia::init::context ctx{};
        ctx.start_info     = start_info;
        const bool init_ok = astia::init::run(ctx);
        astia::log::info("init result=%s", init_ok ? "ok" : "fail");

        if (init_ok && !astia::arch::init_scheduler()) {
            astia::core::kpanic("SMP scheduler initialization failed");
        }

        if (init_ok && !astia::mod::run_selftests()) {
            astia::core::kpanic("NVMe sector selftest failed");
        }

        if (!init_ok || (ASTIA_RUN_SELFTEST && !astia::test::run())) {
            astia::core::kpanic("kernel integration selftest failed");
        }

        if (ASTIA_RUN_LIFECYCLE && ctx.info != nullptr && ctx.info->initrd.paddr != 0) {
            // Two userspace images, each judged by its exit status: the raw-ABI
            // fixture as PID 1, then the same tree's libc as a real C program
            // (CRT0, TLS, stdio over the rings).  The bootstrap thread owns both
            // waits; no intermediate kernel task sits above PID 1.
            char* init_argv[] = {const_cast<char*>("/sbin/init"),
                                 const_cast<char*>("--flag"), nullptr};
            char* init_envp[] = {const_cast<char*>("HOME=/"), nullptr};
            char* libc_argv[] = {const_cast<char*>("/bin/init_libc"), nullptr};
            char* libc_envp[] = {const_cast<char*>("HOME=/"), nullptr};
            astia::arch::cpu::interrupts();
            auto lifecycle_result =
                co_await run_lifecycle(init_argv, init_envp, libc_argv, libc_envp);
            const astia::i64 lifecycle = lifecycle_result ? lifecycle_result.value : -1;
            astia::arch::cpu::nointerrupts();
            if (lifecycle != 0)
                astia::core::kpanic("unable to spawn/reap userspace images");
            astia::log::info("init lifecycle ok");
        }

        astia::arch::cpu::nointerrupts();
        if (ASTIA_RUN_SELFTEST)
            astia::dev::uart16550_console_puts("core: selftest ok\n");
        else
            astia::dev::uart16550_console_puts("core: runtime ok\n");
        astia::dev::debug_exit(0);
        co_return astia::error_t::enone;
    }

} // namespace

namespace {
    [[noreturn]] void halt() {
        for (;;) {
            astia::arch::cpu::halt();
        }
    }

} // namespace

extern "C" void kmain_entry(void* start_info) {
    (void) astia::proc::run_boot(kmain(start_info));
    astia::arch::cpu::nointerrupts();
    halt();
}

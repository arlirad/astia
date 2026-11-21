#include <astia/arch/cpu.hpp>
#include <astia/arch/platform.hpp>
#include <astia/boot.hpp>
#include <astia/common.hpp>
#include <astia/fs.hpp>
#include <astia/init.hpp>
#include <astia/log.hpp>

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
    bool run_image(const char* path, char* const argv[], char* const envp[],
                   bool& spawned) {
        spawned           = false;
        auto spawned_task = spawn_image(astia::proc::current_process(), path, argv, envp);
        spawned_task.resume();
        while (!spawned_task.done())
            astia::proc::scheduler::yield();
        astia::mm::shptr<astia::proc::process> process;
        if (spawned_task.value() != nullptr)
            process = *spawned_task.value();
        if (!process) {
            return false;
        }
        spawned = true;
        const astia::i32 child_pid =
            static_cast<astia::i32>(static_cast<astia::u32>(process->pid()));
        astia::i32       status = -1;
        const astia::i64 reaped = astia::proc::waitpid(child_pid, &status);
        // waitpid re-enables IRQs before returning. Keep process teardown on
        // this lifecycle stack non-preemptible.
        astia::arch::cpu::nointerrupts();
        astia::log::info("spawn reaped pid=%ld status=%d path=%s",
                         static_cast<long>(reaped), status, path);
        const bool ok = reaped == child_pid && status == 0;
        process       = {};
        return ok;
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
        char* init_argv[] = {const_cast<char*>("/sbin/init"), const_cast<char*>("--flag"),
                             nullptr};
        char* init_envp[] = {const_cast<char*>("HOME=/"), nullptr};
        char* libc_argv[] = {const_cast<char*>("/bin/init_libc"), nullptr};
        char* libc_envp[] = {const_cast<char*>("HOME=/"), nullptr};
        astia::arch::cpu::interrupts();
        bool       spawned      = false;
        const bool init_ok      = run_image("/sbin/init", init_argv, init_envp, spawned);
        bool       lifecycle_ok = spawned && init_ok;
        if (lifecycle_ok) {
            const bool libc_ok =
                run_image("/bin/init_libc", libc_argv, libc_envp, spawned);
            lifecycle_ok = spawned && libc_ok;
        }
        astia::arch::cpu::nointerrupts();
        if (!lifecycle_ok)
            astia::core::kpanic("unable to spawn/reap userspace images");
        astia::log::info("init lifecycle ok");
    }

    astia::arch::cpu::nointerrupts();
    if (ASTIA_RUN_SELFTEST)
        astia::dev::uart16550_console_puts("core: selftest ok\n");
    else
        astia::dev::uart16550_console_puts("core: runtime ok\n");
    astia::dev::debug_exit(0);
    halt();
}

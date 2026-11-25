#include <astia/arch/cpu.hpp>
#include <astia/arch/platform.hpp>
#include <astia/boot.hpp>
#include <astia/boot/init.hpp>
#include <astia/common.hpp>
#include <astia/fs.hpp>
#include <astia/init.hpp>
#include <astia/log.hpp>
#include <astia/version.hpp>

#include "dev/chr/tty/uart16550.hpp"
#include "panic.hpp"
#include "proc/proc.hpp"
#include "proc/scheduler.hpp"
#include "sys/syscall.hpp"

namespace {
    [[noreturn]] void launch_failed(const char* path) {
        astia::arch::cpu::nointerrupts();
        astia::log::error("unable to launch init path=%s", path);
        astia::core::kpanic("init launch failed");
    }

    /// The whole kernel boot flow, from early init through init's reap.
    /// Early init runs synchronously before the first co_await; the first
    /// suspension only happens once the scheduler is up.
    astia::core::task<void> kmain(void* start_info) {
        astia::log::info("version=%s", ASTIA_VERSION_STRING);
        astia::log::info("kmain seed start_info=%p", start_info);

        astia::init::context ctx{};
        ctx.start_info = start_info;

        const bool ok = astia::init::run(ctx);
        astia::log::info("init result=%s", ok ? "ok" : "fail");

        if (!ok || !astia::sys::validate_registry()) {
            astia::core::kpanic("syscall registry invalid");
        }

        const astia::boot::init_selection selected =
            astia::boot::select_init(ctx.info == nullptr ? nullptr : ctx.info->cmdline);
        if (selected.error != astia::boot::init_option_error::none)
            astia::core::kpanic("invalid --init command-line option");

        if (ok && !astia::arch::init_scheduler()) {
            astia::core::kpanic("SMP scheduler initialization failed");
        }

        if (!ok) {
            astia::core::kpanic("init failed");
        }

        const char* path = selected.path;
        auto        file = astia::fs::open_path(path);
        if (!file)
            launch_failed(path);
        const long size = file->size();
        if (size <= 0)
            launch_failed(path);
        char* argv[] = {const_cast<char*>(path), nullptr};
        char* envp[] = {const_cast<char*>("HOME=/"), nullptr};

        astia::arch::cpu::interrupts();
        auto result =
            co_await astia::proc::spawnve(astia::proc::kernel_process(), file,
                                          static_cast<astia::usize>(size), argv, envp);
        if (!result)
            launch_failed(path);

        astia::mm::shptr<astia::proc::process> init_process =
            static_cast<astia::mm::shptr<astia::proc::process>&&>(result.value);
        const astia::i32 init_pid =
            static_cast<astia::i32>(static_cast<astia::u32>(init_process->pid()));
        astia::log::info("init launched pid=%d", init_pid);

        astia::i32       exit_status = -1;
        auto             waited = co_await astia::proc::waitpid(init_pid, &exit_status);
        const astia::i64 reaped = waited ? waited.value : -1;

        astia::arch::cpu::nointerrupts();
        astia::log::error("init exited pid=%d status=%d wait=%ld", init_pid, exit_status,
                          static_cast<long>(reaped));
        init_process = {};
        astia::core::kpanic("init terminated");
        co_return astia::error_t::enone;
    }
} // namespace

extern "C" void kmain_entry(void* start_info) {
    (void) astia::proc::run_boot(kmain(start_info));
    astia::arch::cpu::nointerrupts();
    astia::core::kpanic("init terminated");
}

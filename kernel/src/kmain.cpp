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
#include "proc/async_syscall.hpp"
#include "proc/proc.hpp"
#include "proc/scheduler.hpp"
#include "sys/syscall.hpp"

namespace {
    astia::core::task<astia::mm::shptr<astia::proc::process>>
    spawn_init(const char* path) {
        auto file = astia::fs::open_path(path);
        if (!file)
            co_return astia::error_t::enoent;
        const long size = file->size();
        if (size <= 0)
            co_return astia::error_t::einval;
        char* argv[] = {const_cast<char*>(path), nullptr};
        char* envp[] = {const_cast<char*>("HOME=/"), nullptr};
        auto  result =
            co_await astia::proc::spawnve(astia::proc::kernel_process(), file,
                                          static_cast<astia::usize>(size), argv, envp);
        if (!result)
            co_return result.error;
        co_return static_cast<astia::mm::shptr<astia::proc::process>&&>(result.value);
    }

} // namespace

extern "C" void kmain(void* start_info) {
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

    astia::arch::cpu::interrupts();

    auto runner = spawn_init(selected.path);
    runner.resume();
    while (!runner.done())
        astia::proc::scheduler::yield();

    astia::mm::shptr<astia::proc::process> init_process;
    if (runner.value() != nullptr)
        init_process = *runner.value();
    if (!init_process) {
        astia::arch::cpu::nointerrupts();
        runner.reset();
        astia::log::error("unable to launch init path=%s", selected.path);
        astia::core::kpanic("init launch failed");
    }

    const astia::i32 init_pid =
        static_cast<astia::i32>(static_cast<astia::u32>(init_process->pid()));
    astia::log::info("init launched pid=%d", init_pid);

    astia::i32       exit_status = -1;
    const astia::i64 reaped =
        astia::proc::run_async_syscall(astia::proc::waitpid(init_pid, &exit_status));

    astia::arch::cpu::nointerrupts();
    astia::log::error("init exited pid=%d status=%d wait=%ld", init_pid, exit_status,
                      static_cast<long>(reaped));
    init_process = {};
    runner.reset();
    astia::core::kpanic("init terminated");
}

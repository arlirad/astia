#include <astia/arch/cpu.hpp>
#include <astia/arch/platform.hpp>
#include <astia/boot.hpp>
#include <astia/boot/init.hpp>
#include <astia/common.hpp>
#include <astia/fs.hpp>
#include <astia/init.hpp>
#include <astia/log.hpp>

#include "dev/chr/tty/uart16550.hpp"
#include "panic.hpp"
#include "proc/proc.hpp"
#include "proc/scheduler.hpp"

namespace {
    volatile astia::u32 init_status = 0;

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

    void supervise_init(void* opaque) {
        const char* path   = static_cast<const char*>(opaque);
        auto        runner = spawn_init(path);
        runner.resume();
        while (!runner.done())
            astia::proc::scheduler::yield();

        astia::mm::shptr<astia::proc::process> process;
        if (runner.value() != nullptr)
            process = *runner.value();
        if (!process) {
            astia::arch::cpu::nointerrupts();
            runner.reset();
            astia::log::error("unable to launch init path=%s", path);
            __atomic_store_n(&init_status, 1u, __ATOMIC_RELEASE);
            astia::proc::current_thread()->exit();
        }

        const astia::i32 child_pid =
            static_cast<astia::i32>(static_cast<astia::u32>(process->pid()));
        astia::i32       exit_status = -1;
        const astia::i64 reaped      = astia::proc::waitpid(child_pid, &exit_status);
        astia::arch::cpu::nointerrupts();
        astia::log::error("init exited pid=%d status=%d wait=%ld", child_pid, exit_status,
                          static_cast<long>(reaped));
        process = {};
        runner.reset();
        __atomic_store_n(&init_status, 2u, __ATOMIC_RELEASE);
        astia::proc::current_thread()->exit();
    }

} // namespace

extern "C" void kmain(void* start_info) {
    astia::log::info("kmain seed start_info=%p", start_info);

    astia::init::context ctx{};
    ctx.start_info = start_info;

    const bool ok = astia::init::run(ctx);
    astia::log::info("init result=%s", ok ? "ok" : "fail");

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

    init_status = 0;
    auto worker = astia::proc::scheduler::create_kernel(supervise_init,
                                                        const_cast<char*>(selected.path));
    if (!worker)
        astia::core::kpanic("unable to create init supervisor");
    astia::arch::cpu::interrupts();
    if (worker->join() != 0 || __atomic_load_n(&init_status, __ATOMIC_ACQUIRE) != 2)
        astia::core::kpanic("init launch failed");
    astia::core::kpanic("init terminated");
}

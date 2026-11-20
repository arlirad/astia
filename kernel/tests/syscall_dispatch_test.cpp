#include <astia/api/errno.h>
#include <astia/api/syscalls.h>
#include <astia/core/completion.hpp>

#include "support/test.hpp"
#include "syscall.hpp"

extern "C" long astia_syscall_dispatch(astia::arch::syscall_frame* frame);

namespace {

    astia::core::completion<astia::i64>* pending_completion = nullptr;

    template <astia::i64 Id>
    void invoke_probe(astia::sys::syscall_context&         context,
                      astia::core::completion<astia::i64>& completion) {
        if constexpr (Id == static_cast<astia::i64>(astia::sys::syscall_id::io_submit)) {
            pending_completion = &completion;
            return;
        }
        else if constexpr (Id ==
                           static_cast<astia::i64>(astia::sys::syscall_id::io_setup)) {
            const auto signed_value   = static_cast<astia::i32>(context.arguments[0]);
            const auto unsigned_value = static_cast<astia::u32>(context.arguments[1]);
            completion.complete(static_cast<astia::i64>(signed_value) + unsigned_value);
        }
        else {
            completion.complete(Id + context.arguments[0]);
        }
    }

    struct io_setup_handler
        : astia::sys::handler<io_setup_handler, astia::sys::syscall_id::io_setup> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<0>(context, completion);
        }
    };
    struct io_destroy_handler
        : astia::sys::handler<io_destroy_handler, astia::sys::syscall_id::io_destroy> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<1>(context, completion);
        }
    };
    struct io_submit_handler
        : astia::sys::handler<io_submit_handler, astia::sys::syscall_id::io_submit> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<2>(context, completion);
        }
    };
    struct io_register_handler
        : astia::sys::handler<io_register_handler, astia::sys::syscall_id::io_register> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<3>(context, completion);
        }
    };
    struct mmap_handler
        : astia::sys::handler<mmap_handler, astia::sys::syscall_id::mmap> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<4>(context, completion);
        }
    };
    struct munmap_handler
        : astia::sys::handler<munmap_handler, astia::sys::syscall_id::munmap> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<5>(context, completion);
        }
    };
    struct mprotect_handler
        : astia::sys::handler<mprotect_handler, astia::sys::syscall_id::mprotect> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<6>(context, completion);
        }
    };
    struct clock_gettime_handler
        : astia::sys::handler<clock_gettime_handler,
                              astia::sys::syscall_id::clock_gettime> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<7>(context, completion);
        }
    };
    struct exit_handler
        : astia::sys::handler<exit_handler, astia::sys::syscall_id::exit> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<8>(context, completion);
        }
    };
    struct yield_handler
        : astia::sys::handler<yield_handler, astia::sys::syscall_id::yield> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<9>(context, completion);
        }
    };
    struct getpid_handler
        : astia::sys::handler<getpid_handler, astia::sys::syscall_id::getpid> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<10>(context, completion);
        }
    };
    struct gettid_handler
        : astia::sys::handler<gettid_handler, astia::sys::syscall_id::gettid> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<11>(context, completion);
        }
    };
    struct futex_handler
        : astia::sys::handler<futex_handler, astia::sys::syscall_id::futex> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<12>(context, completion);
        }
    };
    struct waitpid_handler
        : astia::sys::handler<waitpid_handler, astia::sys::syscall_id::waitpid> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<13>(context, completion);
        }
    };
    struct spawnve_handler
        : astia::sys::handler<spawnve_handler, astia::sys::syscall_id::spawnve> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<14>(context, completion);
        }
    };
    struct fork_handler
        : astia::sys::handler<fork_handler, astia::sys::syscall_id::fork> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<15>(context, completion);
        }
    };
    struct thread_create_handler
        : astia::sys::handler<thread_create_handler,
                              astia::sys::syscall_id::thread_create> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<16>(context, completion);
        }
    };
    struct thread_exit_handler
        : astia::sys::handler<thread_exit_handler, astia::sys::syscall_id::thread_exit> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<17>(context, completion);
        }
    };
    struct thread_join_handler
        : astia::sys::handler<thread_join_handler, astia::sys::syscall_id::thread_join> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<18>(context, completion);
        }
    };
    struct thread_detach_handler
        : astia::sys::handler<thread_detach_handler,
                              astia::sys::syscall_id::thread_detach> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<19>(context, completion);
        }
    };
    struct kill_handler
        : astia::sys::handler<kill_handler, astia::sys::syscall_id::kill> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<20>(context, completion);
        }
    };
    struct sigaction_handler
        : astia::sys::handler<sigaction_handler, astia::sys::syscall_id::sigaction> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<21>(context, completion);
        }
    };
    struct sigprocmask_handler
        : astia::sys::handler<sigprocmask_handler, astia::sys::syscall_id::sigprocmask> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<22>(context, completion);
        }
    };
    struct thread_create2_handler
        : astia::sys::handler<thread_create2_handler,
                              astia::sys::syscall_id::thread_create2> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<23>(context, completion);
        }
    };
    struct execve_handler
        : astia::sys::handler<execve_handler, astia::sys::syscall_id::execve> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<24>(context, completion);
        }
    };
    struct sigsuspend_handler
        : astia::sys::handler<sigsuspend_handler, astia::sys::syscall_id::sigsuspend> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<25>(context, completion);
        }
    };
    struct getpgrp_handler
        : astia::sys::handler<getpgrp_handler, astia::sys::syscall_id::getpgrp> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<26>(context, completion);
        }
    };
    struct setpgid_handler
        : astia::sys::handler<setpgid_handler, astia::sys::syscall_id::setpgid> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<27>(context, completion);
        }
    };
    struct setsid_handler
        : astia::sys::handler<setsid_handler, astia::sys::syscall_id::setsid> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<28>(context, completion);
        }
    };
    struct sigreturn_handler
        : astia::sys::handler<sigreturn_handler, astia::sys::syscall_id::sigreturn> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<29>(context, completion);
        }
    };
    struct chdir_handler
        : astia::sys::handler<chdir_handler, astia::sys::syscall_id::chdir> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<30>(context, completion);
        }
    };
    struct getcwd_handler
        : astia::sys::handler<getcwd_handler, astia::sys::syscall_id::getcwd> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<31>(context, completion);
        }
    };
    struct getuid_handler
        : astia::sys::handler<getuid_handler, astia::sys::syscall_id::getuid> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<32>(context, completion);
        }
    };
    struct geteuid_handler
        : astia::sys::handler<geteuid_handler, astia::sys::syscall_id::geteuid> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<33>(context, completion);
        }
    };
    struct getgid_handler
        : astia::sys::handler<getgid_handler, astia::sys::syscall_id::getgid> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<34>(context, completion);
        }
    };
    struct getegid_handler
        : astia::sys::handler<getegid_handler, astia::sys::syscall_id::getegid> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<35>(context, completion);
        }
    };
    struct setuid_handler
        : astia::sys::handler<setuid_handler, astia::sys::syscall_id::setuid> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<36>(context, completion);
        }
    };
    struct seteuid_handler
        : astia::sys::handler<seteuid_handler, astia::sys::syscall_id::seteuid> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<37>(context, completion);
        }
    };
    struct setgid_handler
        : astia::sys::handler<setgid_handler, astia::sys::syscall_id::setgid> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<38>(context, completion);
        }
    };
    struct setegid_handler
        : astia::sys::handler<setegid_handler, astia::sys::syscall_id::setegid> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<39>(context, completion);
        }
    };
    struct setreuid_handler
        : astia::sys::handler<setreuid_handler, astia::sys::syscall_id::setreuid> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<40>(context, completion);
        }
    };
    struct setregid_handler
        : astia::sys::handler<setregid_handler, astia::sys::syscall_id::setregid> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<41>(context, completion);
        }
    };
    struct getgroups_handler
        : astia::sys::handler<getgroups_handler, astia::sys::syscall_id::getgroups> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<42>(context, completion);
        }
    };
    struct setgroups_handler
        : astia::sys::handler<setgroups_handler, astia::sys::syscall_id::setgroups> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<43>(context, completion);
        }
    };
    struct process_snapshot_handler
        : astia::sys::handler<process_snapshot_handler,
                              astia::sys::syscall_id::process_snapshot> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<44>(context, completion);
        }
    };
    struct getsid_handler
        : astia::sys::handler<getsid_handler, astia::sys::syscall_id::getsid> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<45>(context, completion);
        }
    };
    struct unmount_handler
        : astia::sys::handler<unmount_handler, astia::sys::syscall_id::unmount> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<46>(context, completion);
        }
    };
    struct mount_handler
        : astia::sys::handler<mount_handler, astia::sys::syscall_id::mount> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<47>(context, completion);
        }
    };
    struct getrlimit_handler
        : astia::sys::handler<getrlimit_handler, astia::sys::syscall_id::getrlimit> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<48>(context, completion);
        }
    };
    struct setrlimit_handler
        : astia::sys::handler<setrlimit_handler, astia::sys::syscall_id::setrlimit> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<49>(context, completion);
        }
    };
    struct sigwait_handler
        : astia::sys::handler<sigwait_handler, astia::sys::syscall_id::sigwait> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<50>(context, completion);
        }
    };
    struct sigaltstack_handler
        : astia::sys::handler<sigaltstack_handler, astia::sys::syscall_id::sigaltstack> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<51>(context, completion);
        }
    };
    struct getpgid_handler
        : astia::sys::handler<getpgid_handler, astia::sys::syscall_id::getpgid> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<52>(context, completion);
        }
    };
    struct getppid_handler
        : astia::sys::handler<getppid_handler, astia::sys::syscall_id::getppid> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<53>(context, completion);
        }
    };
    struct thread_cancel_handler
        : astia::sys::handler<thread_cancel_handler,
                              astia::sys::syscall_id::thread_cancel> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<54>(context, completion);
        }
    };
    struct alarm_handler
        : astia::sys::handler<alarm_handler, astia::sys::syscall_id::alarm> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<55>(context, completion);
        }
    };
    struct thread_create_owned_handler
        : astia::sys::handler<thread_create_owned_handler,
                              astia::sys::syscall_id::thread_create_owned> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<56>(context, completion);
        }
    };
    struct thread_cancel_pending_handler
        : astia::sys::handler<thread_cancel_pending_handler,
                              astia::sys::syscall_id::thread_cancel_pending> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<57>(context, completion);
        }
    };
    struct thread_exists_handler
        : astia::sys::handler<thread_exists_handler,
                              astia::sys::syscall_id::thread_exists> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<58>(context, completion);
        }
    };
    struct thread_kill_handler
        : astia::sys::handler<thread_kill_handler, astia::sys::syscall_id::thread_kill> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<59>(context, completion);
        }
    };
    struct sigpending_handler
        : astia::sys::handler<sigpending_handler, astia::sys::syscall_id::sigpending> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<60>(context, completion);
        }
    };
    struct sigtimedwait_handler
        : astia::sys::handler<sigtimedwait_handler,
                              astia::sys::syscall_id::sigtimedwait> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<61>(context, completion);
        }
    };
    struct clock_getres_handler
        : astia::sys::handler<clock_getres_handler,
                              astia::sys::syscall_id::clock_getres> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<62>(context, completion);
        }
    };
    struct thread_getattr_handler
        : astia::sys::handler<thread_getattr_handler,
                              astia::sys::syscall_id::thread_getattr> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<63>(context, completion);
        }
    };
    struct msync_handler
        : astia::sys::handler<msync_handler, astia::sys::syscall_id::msync> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<64>(context, completion);
        }
    };
    struct lseek_handler
        : astia::sys::handler<lseek_handler, astia::sys::syscall_id::lseek> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<65>(context, completion);
        }
    };
    struct umask_handler
        : astia::sys::handler<umask_handler, astia::sys::syscall_id::umask> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<66>(context, completion);
        }
    };
    struct shm_open_handler
        : astia::sys::handler<shm_open_handler, astia::sys::syscall_id::shm_open> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<67>(context, completion);
        }
    };
    struct shm_unlink_handler
        : astia::sys::handler<shm_unlink_handler, astia::sys::syscall_id::shm_unlink> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<68>(context, completion);
        }
    };
    struct fchdir_handler
        : astia::sys::handler<fchdir_handler, astia::sys::syscall_id::fchdir> {
        static void invoke(astia::sys::syscall_context&         context,
                           astia::core::completion<astia::i64>& completion) {
            invoke_probe<69>(context, completion);
        }
    };
    struct duplicate_handler
        : astia::sys::handler<duplicate_handler, astia::sys::syscall_id::getpid> {
        static void invoke(astia::sys::syscall_context&,
                           astia::core::completion<astia::i64>& completion) {
            completion.complete(0);
        }
    };

    struct out_of_range_handler
        : astia::sys::handler<out_of_range_handler,
                              static_cast<astia::sys::syscall_id>(
                                  astia::sys::syscall_table_capacity)> {
        static void invoke(astia::sys::syscall_context&,
                           astia::core::completion<astia::i64>& completion) {
            completion.complete(0);
        }
    };

    template <typename... Handlers>
    struct descriptor_pack {
        static constexpr bool valid_ids =
            astia::sys::detail::valid_descriptor_ids<Handlers...>();

        static bool dispatch(astia::sys::syscall_id               id,
                             astia::sys::syscall_context&         context,
                             astia::core::completion<astia::i64>& completion) {
            return astia::sys::dispatch_async<Handlers...>(id, context, completion);
        }
    };

    using prototype_handlers = descriptor_pack<
        io_setup_handler, io_destroy_handler, io_submit_handler, io_register_handler,
        mmap_handler, munmap_handler, mprotect_handler, clock_gettime_handler,
        exit_handler, yield_handler, getpid_handler, gettid_handler, futex_handler,
        waitpid_handler, spawnve_handler, fork_handler, thread_create_handler,
        thread_exit_handler, thread_join_handler, thread_detach_handler, kill_handler,
        sigaction_handler, sigprocmask_handler, thread_create2_handler, execve_handler,
        sigsuspend_handler, getpgrp_handler, setpgid_handler, setsid_handler,
        sigreturn_handler, chdir_handler, getcwd_handler, getuid_handler, geteuid_handler,
        getgid_handler, getegid_handler, setuid_handler, seteuid_handler, setgid_handler,
        setegid_handler, setreuid_handler, setregid_handler, getgroups_handler,
        setgroups_handler, process_snapshot_handler, getsid_handler, unmount_handler,
        mount_handler, getrlimit_handler, setrlimit_handler, sigwait_handler,
        sigaltstack_handler, getpgid_handler, getppid_handler, thread_cancel_handler,
        alarm_handler, thread_create_owned_handler, thread_cancel_pending_handler,
        thread_exists_handler, thread_kill_handler, sigpending_handler,
        sigtimedwait_handler, clock_getres_handler, thread_getattr_handler, msync_handler,
        lseek_handler, umask_handler, shm_open_handler, shm_unlink_handler,
        fchdir_handler>;

    static_assert(prototype_handlers::valid_ids);
    static_assert(
        !astia::sys::detail::valid_descriptor_ids<getpid_handler, duplicate_handler>());
    static_assert(!astia::sys::detail::valid_descriptor_ids<out_of_range_handler>());
    static_assert(static_cast<astia::u32>(astia::sys::syscall_id::getpid) == SYS_GETPID);
    static_assert(static_cast<astia::u32>(astia::sys::syscall_id::io_submit) ==
                  SYS_IO_SUBMIT);

    astia::sys::syscall_context context_for(astia::sys::syscall_id            id,
                                            const astia::arch::syscall_frame& frame) {
        return {id, {-3, 8, 0, 0, 0, 0}, frame, {0x1A2B}, {0xC0DE}};
    }

} // namespace

ASTIA_TEST(all_prototype_syscall_ids_dispatch_through_async_crtp_descriptors) {
    astia::arch::syscall_frame frame{};
    for (astia::u32 number = 0;
         number <= static_cast<astia::u32>(astia::sys::syscall_id::fchdir); ++number) {
        const auto id = static_cast<astia::sys::syscall_id>(number);
        if (id == astia::sys::syscall_id::io_submit)
            continue;
        auto                                context = context_for(id, frame);
        astia::core::completion<astia::i64> result;
        ASTIA_CHECK((prototype_handlers::dispatch(id, context, result)));
        ASTIA_CHECK(result.done());
        const astia::i64 expected = id == astia::sys::syscall_id::io_setup
                                        ? 5
                                        : static_cast<astia::i64>(number) - 3;
        auto             value    = result.await_resume();
        ASTIA_CHECK(value && value.value == expected);
        ASTIA_CHECK(context.operation.value == 0x1A2B);
        ASTIA_CHECK(context.user_data.value == 0xC0DE);
    }
}

ASTIA_TEST(async_syscall_can_remain_pending_then_complete_or_cancel_once) {
    astia::arch::syscall_frame frame{};
    auto context = context_for(astia::sys::syscall_id::io_submit, frame);
    astia::core::completion<astia::i64> result;
    ASTIA_CHECK((prototype_handlers::dispatch(context.id, context, result)));
    ASTIA_CHECK(!result.done());
    ASTIA_CHECK(pending_completion == &result);
    ASTIA_CHECK(result.complete(42));
    ASTIA_CHECK(!result.complete(43));
    ASTIA_CHECK(result.done());
    auto completed = result.await_resume();
    ASTIA_CHECK(completed && completed.value == 42);

    astia::core::completion<astia::i64> canceled;
    auto cancel_context = context_for(astia::sys::syscall_id::io_submit, frame);
    ASTIA_CHECK(
        prototype_handlers::dispatch(cancel_context.id, cancel_context, canceled));
    ASTIA_CHECK(!canceled.done());
    ASTIA_CHECK(canceled.cancel());
    ASTIA_CHECK(!canceled.cancel());
    ASTIA_CHECK(!canceled.complete(1));
    ASTIA_CHECK(canceled.done());
    auto canceled_result = canceled.await_resume();
    ASTIA_CHECK(!canceled_result);
}

ASTIA_TEST(async_syscall_dispatch_completes_unknown_ids_with_enosys) {
    astia::arch::syscall_frame frame{};
    const astia::u32           unknown_numbers[] = {70, 128};
    for (const auto number : unknown_numbers) {
        auto context = context_for(static_cast<astia::sys::syscall_id>(number), frame);
        astia::core::completion<astia::i64> result;
        ASTIA_CHECK(!(prototype_handlers::dispatch(context.id, context, result)));
        ASTIA_CHECK(result.done());
        auto value = result.await_resume();
        ASTIA_CHECK(value && value.value == -ENOSYS);
    }
}

ASTIA_TEST(architecture_entry_preserves_existing_synchronous_syscall_abi) {
    constexpr auto handler =
        +[](astia::i64 value, astia::u32 flags) -> astia::i64 { return value - flags; };
    ASTIA_CHECK((
        astia::sys::register_syscall<handler>(static_cast<astia::sys::syscall_id>(124))));
    astia::arch::syscall_frame frame{};
    frame.nr = 124;
    frame.a0 = 19;
    frame.a1 = 4;

    ASTIA_CHECK(astia_syscall_dispatch(&frame) == 15);
    ASTIA_CHECK(frame.nr == 15);
}

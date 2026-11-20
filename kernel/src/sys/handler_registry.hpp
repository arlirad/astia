#pragma once

#include "../fs/syscalls/chdir.hpp"
#include "../fs/syscalls/fchdir.hpp"
#include "../fs/syscalls/getcwd.hpp"
#include "../fs/syscalls/io_destroy.hpp"
#include "../fs/syscalls/io_register.hpp"
#include "../fs/syscalls/io_setup.hpp"
#include "../fs/syscalls/io_submit.hpp"
#include "../fs/syscalls/lseek.hpp"
#include "../fs/syscalls/mount.hpp"
#include "../fs/syscalls/shm_open.hpp"
#include "../fs/syscalls/shm_unlink.hpp"
#include "../fs/syscalls/umask.hpp"
#include "../fs/syscalls/unmount.hpp"
#include "../ipc/syscalls/futex.hpp"
#include "../mm/syscalls/mmap.hpp"
#include "../mm/syscalls/mprotect.hpp"
#include "../mm/syscalls/msync.hpp"
#include "../mm/syscalls/munmap.hpp"
#include "../net/syscalls/accept.hpp"
#include "../net/syscalls/bind.hpp"
#include "../net/syscalls/connect.hpp"
#include "../net/syscalls/getsockopt.hpp"
#include "../net/syscalls/listen.hpp"
#include "../net/syscalls/recv.hpp"
#include "../net/syscalls/recvfrom.hpp"
#include "../net/syscalls/send.hpp"
#include "../net/syscalls/sendto.hpp"
#include "../net/syscalls/shutdown.hpp"
#include "../net/syscalls/socket.hpp"
#include "../net/syscalls/socketpair.hpp"
#include "../proc/syscalls/alarm.hpp"
#include "../proc/syscalls/execve.hpp"
#include "../proc/syscalls/exit.hpp"
#include "../proc/syscalls/fork.hpp"
#include "../proc/syscalls/getegid.hpp"
#include "../proc/syscalls/geteuid.hpp"
#include "../proc/syscalls/getgid.hpp"
#include "../proc/syscalls/getgroups.hpp"
#include "../proc/syscalls/getpgid.hpp"
#include "../proc/syscalls/getpgrp.hpp"
#include "../proc/syscalls/getpid.hpp"
#include "../proc/syscalls/getppid.hpp"
#include "../proc/syscalls/getrlimit.hpp"
#include "../proc/syscalls/getsid.hpp"
#include "../proc/syscalls/gettid.hpp"
#include "../proc/syscalls/getuid.hpp"
#include "../proc/syscalls/kill.hpp"
#include "../proc/syscalls/process_snapshot.hpp"
#include "../proc/syscalls/setegid.hpp"
#include "../proc/syscalls/seteuid.hpp"
#include "../proc/syscalls/setgid.hpp"
#include "../proc/syscalls/setgroups.hpp"
#include "../proc/syscalls/setpgid.hpp"
#include "../proc/syscalls/setregid.hpp"
#include "../proc/syscalls/setreuid.hpp"
#include "../proc/syscalls/setrlimit.hpp"
#include "../proc/syscalls/setsid.hpp"
#include "../proc/syscalls/setuid.hpp"
#include "../proc/syscalls/sigaction.hpp"
#include "../proc/syscalls/sigaltstack.hpp"
#include "../proc/syscalls/sigpending.hpp"
#include "../proc/syscalls/sigprocmask.hpp"
#include "../proc/syscalls/sigreturn.hpp"
#include "../proc/syscalls/sigsuspend.hpp"
#include "../proc/syscalls/sigtimedwait.hpp"
#include "../proc/syscalls/sigwait.hpp"
#include "../proc/syscalls/spawnve.hpp"
#include "../proc/syscalls/thread_cancel.hpp"
#include "../proc/syscalls/thread_cancel_pending.hpp"
#include "../proc/syscalls/thread_create.hpp"
#include "../proc/syscalls/thread_create2.hpp"
#include "../proc/syscalls/thread_create_owned.hpp"
#include "../proc/syscalls/thread_detach.hpp"
#include "../proc/syscalls/thread_exists.hpp"
#include "../proc/syscalls/thread_exit.hpp"
#include "../proc/syscalls/thread_getattr.hpp"
#include "../proc/syscalls/thread_join.hpp"
#include "../proc/syscalls/thread_kill.hpp"
#include "../proc/syscalls/waitpid.hpp"
#include "../proc/syscalls/yield.hpp"
#include "../sys/syscalls/clock_getres.hpp"
#include "../sys/syscalls/clock_gettime.hpp"
#include "syscall.hpp"

namespace astia::sys {
    template <typename... types>
    struct type_list {};
    namespace production {
        template <typename... types>
        using type_list   = ::astia::sys::type_list<types...>;
        using handler_set = type_list<
            fs::syscall_handlers::io_setup_syscall,
            fs::syscall_handlers::io_destroy_syscall,
            fs::syscall_handlers::io_submit_syscall,
            fs::syscall_handlers::io_register_syscall, mm::syscall_handlers::mmap_syscall,
            mm::syscall_handlers::munmap_syscall, mm::syscall_handlers::mprotect_syscall,
            sys::syscall_handlers::clock_gettime_syscall,
            proc::syscall_handlers::exit_syscall, proc::syscall_handlers::yield_syscall,
            proc::syscall_handlers::getpid_syscall,
            proc::syscall_handlers::gettid_syscall, ipc::syscall_handlers::futex_syscall,
            proc::syscall_handlers::waitpid_syscall,
            proc::syscall_handlers::spawnve_syscall, proc::syscall_handlers::fork_syscall,
            proc::syscall_handlers::thread_create_syscall,
            proc::syscall_handlers::thread_exit_syscall,
            proc::syscall_handlers::thread_join_syscall,
            proc::syscall_handlers::thread_detach_syscall,
            proc::syscall_handlers::kill_syscall,
            proc::syscall_handlers::sigaction_syscall,
            proc::syscall_handlers::sigprocmask_syscall,
            proc::syscall_handlers::thread_create2_syscall,
            proc::syscall_handlers::execve_syscall,
            proc::syscall_handlers::sigsuspend_syscall,
            proc::syscall_handlers::getpgrp_syscall,
            proc::syscall_handlers::setpgid_syscall,
            proc::syscall_handlers::setsid_syscall,
            proc::syscall_handlers::sigreturn_syscall,
            fs::syscall_handlers::chdir_syscall, fs::syscall_handlers::getcwd_syscall,
            proc::syscall_handlers::getuid_syscall,
            proc::syscall_handlers::geteuid_syscall,
            proc::syscall_handlers::getgid_syscall,
            proc::syscall_handlers::getegid_syscall,
            proc::syscall_handlers::setuid_syscall,
            proc::syscall_handlers::seteuid_syscall,
            proc::syscall_handlers::setgid_syscall,
            proc::syscall_handlers::setegid_syscall,
            proc::syscall_handlers::setreuid_syscall,
            proc::syscall_handlers::setregid_syscall,
            proc::syscall_handlers::getgroups_syscall,
            proc::syscall_handlers::setgroups_syscall,
            proc::syscall_handlers::process_snapshot_syscall,
            proc::syscall_handlers::getsid_syscall, fs::syscall_handlers::unmount_syscall,
            fs::syscall_handlers::mount_syscall,
            proc::syscall_handlers::getrlimit_syscall,
            proc::syscall_handlers::setrlimit_syscall,
            proc::syscall_handlers::sigwait_syscall,
            proc::syscall_handlers::sigaltstack_syscall,
            proc::syscall_handlers::getpgid_syscall,
            proc::syscall_handlers::getppid_syscall,
            proc::syscall_handlers::thread_cancel_syscall,
            proc::syscall_handlers::alarm_syscall,
            proc::syscall_handlers::thread_create_owned_syscall,
            proc::syscall_handlers::thread_cancel_pending_syscall,
            proc::syscall_handlers::thread_exists_syscall,
            proc::syscall_handlers::thread_kill_syscall,
            proc::syscall_handlers::sigpending_syscall,
            proc::syscall_handlers::sigtimedwait_syscall,
            sys::syscall_handlers::clock_getres_syscall,
            proc::syscall_handlers::thread_getattr_syscall,
            mm::syscall_handlers::msync_syscall, fs::syscall_handlers::lseek_syscall,
            fs::syscall_handlers::umask_syscall, fs::syscall_handlers::shm_open_syscall,
            fs::syscall_handlers::shm_unlink_syscall,
            fs::syscall_handlers::fchdir_syscall>;

    } // namespace production

    namespace net {
        using handler_set = type_list<::astia::net::syscall_handlers::accept_syscall,
                                      ::astia::net::syscall_handlers::bind_syscall,
                                      ::astia::net::syscall_handlers::connect_syscall,
                                      ::astia::net::syscall_handlers::getsockopt_syscall,
                                      ::astia::net::syscall_handlers::listen_syscall,
                                      ::astia::net::syscall_handlers::recv_syscall,
                                      ::astia::net::syscall_handlers::recvfrom_syscall,
                                      ::astia::net::syscall_handlers::send_syscall,
                                      ::astia::net::syscall_handlers::sendto_syscall,
                                      ::astia::net::syscall_handlers::shutdown_syscall,
                                      ::astia::net::syscall_handlers::socket_syscall,
                                      ::astia::net::syscall_handlers::socketpair_syscall>;

    } // namespace net
} // namespace astia::sys

#pragma once

#include "fs/syscalls/chdir.hpp"
#include "fs/syscalls/fchdir.hpp"
#include "fs/syscalls/getcwd.hpp"
#include "fs/syscalls/io_destroy.hpp"
#include "fs/syscalls/io_register.hpp"
#include "fs/syscalls/io_setup.hpp"
#include "fs/syscalls/io_submit.hpp"
#include "fs/syscalls/lseek.hpp"
#include "fs/syscalls/mount.hpp"
#include "fs/syscalls/shm_open.hpp"
#include "fs/syscalls/shm_unlink.hpp"
#include "fs/syscalls/umask.hpp"
#include "fs/syscalls/unmount.hpp"
#include "ipc/syscalls/futex.hpp"
#include "mm/syscalls/mmap.hpp"
#include "mm/syscalls/mprotect.hpp"
#include "mm/syscalls/msync.hpp"
#include "mm/syscalls/munmap.hpp"
#include "net/syscalls/accept.hpp"
#include "net/syscalls/bind.hpp"
#include "net/syscalls/connect.hpp"
#include "net/syscalls/getsockopt.hpp"
#include "net/syscalls/listen.hpp"
#include "net/syscalls/recv.hpp"
#include "net/syscalls/recvfrom.hpp"
#include "net/syscalls/send.hpp"
#include "net/syscalls/sendto.hpp"
#include "net/syscalls/shutdown.hpp"
#include "net/syscalls/socket.hpp"
#include "net/syscalls/socketpair.hpp"
#include "proc/syscalls/alarm.hpp"
#include "proc/syscalls/execve.hpp"
#include "proc/syscalls/exit.hpp"
#include "proc/syscalls/fork.hpp"
#include "proc/syscalls/getegid.hpp"
#include "proc/syscalls/geteuid.hpp"
#include "proc/syscalls/getgid.hpp"
#include "proc/syscalls/getgroups.hpp"
#include "proc/syscalls/getpgid.hpp"
#include "proc/syscalls/getpgrp.hpp"
#include "proc/syscalls/getpid.hpp"
#include "proc/syscalls/getppid.hpp"
#include "proc/syscalls/getrlimit.hpp"
#include "proc/syscalls/getsid.hpp"
#include "proc/syscalls/gettid.hpp"
#include "proc/syscalls/getuid.hpp"
#include "proc/syscalls/kill.hpp"
#include "proc/syscalls/process_snapshot.hpp"
#include "proc/syscalls/setegid.hpp"
#include "proc/syscalls/seteuid.hpp"
#include "proc/syscalls/setgid.hpp"
#include "proc/syscalls/setgroups.hpp"
#include "proc/syscalls/setpgid.hpp"
#include "proc/syscalls/setregid.hpp"
#include "proc/syscalls/setreuid.hpp"
#include "proc/syscalls/setrlimit.hpp"
#include "proc/syscalls/setsid.hpp"
#include "proc/syscalls/setuid.hpp"
#include "proc/syscalls/sigaction.hpp"
#include "proc/syscalls/sigaltstack.hpp"
#include "proc/syscalls/sigpending.hpp"
#include "proc/syscalls/sigprocmask.hpp"
#include "proc/syscalls/sigreturn.hpp"
#include "proc/syscalls/sigsuspend.hpp"
#include "proc/syscalls/sigtimedwait.hpp"
#include "proc/syscalls/sigwait.hpp"
#include "proc/syscalls/spawnve.hpp"
#include "proc/syscalls/thread_cancel.hpp"
#include "proc/syscalls/thread_cancel_pending.hpp"
#include "proc/syscalls/thread_create.hpp"
#include "proc/syscalls/thread_create2.hpp"
#include "proc/syscalls/thread_create_owned.hpp"
#include "proc/syscalls/thread_detach.hpp"
#include "proc/syscalls/thread_exists.hpp"
#include "proc/syscalls/thread_exit.hpp"
#include "proc/syscalls/thread_getattr.hpp"
#include "proc/syscalls/thread_join.hpp"
#include "proc/syscalls/thread_kill.hpp"
#include "proc/syscalls/waitpid.hpp"
#include "proc/syscalls/yield.hpp"
#include "sys/syscalls/clock_getres.hpp"
#include "sys/syscalls/clock_gettime.hpp"

#include <syscall.hpp>

namespace astia::sys::prototype {
    template <typename... Types>
    struct type_list {};

    using handler_set = type_list<
        fs::syscall_prototype::IoSetupHandler, fs::syscall_prototype::IoDestroyHandler,
        fs::syscall_prototype::IoSubmitHandler, fs::syscall_prototype::IoRegisterHandler,
        mm::syscall_prototype::MmapHandler, mm::syscall_prototype::MunmapHandler,
        mm::syscall_prototype::MprotectHandler,
        sys::syscall_prototype::ClockGettimeHandler, proc::syscall_prototype::ExitHandler,
        proc::syscall_prototype::YieldHandler, proc::syscall_prototype::GetpidHandler,
        proc::syscall_prototype::GettidHandler, ipc::syscall_prototype::FutexHandler,
        proc::syscall_prototype::WaitpidHandler, proc::syscall_prototype::SpawnveHandler,
        proc::syscall_prototype::ForkHandler,
        proc::syscall_prototype::ThreadCreateHandler,
        proc::syscall_prototype::ThreadExitHandler,
        proc::syscall_prototype::ThreadJoinHandler,
        proc::syscall_prototype::ThreadDetachHandler,
        proc::syscall_prototype::KillHandler, proc::syscall_prototype::SigactionHandler,
        proc::syscall_prototype::SigprocmaskHandler,
        proc::syscall_prototype::ThreadCreate2Handler,
        proc::syscall_prototype::ExecveHandler,
        proc::syscall_prototype::SigsuspendHandler,
        proc::syscall_prototype::GetpgrpHandler, proc::syscall_prototype::SetpgidHandler,
        proc::syscall_prototype::SetsidHandler, proc::syscall_prototype::SigreturnHandler,
        fs::syscall_prototype::ChdirHandler, fs::syscall_prototype::GetcwdHandler,
        proc::syscall_prototype::GetuidHandler, proc::syscall_prototype::GeteuidHandler,
        proc::syscall_prototype::GetgidHandler, proc::syscall_prototype::GetegidHandler,
        proc::syscall_prototype::SetuidHandler, proc::syscall_prototype::SeteuidHandler,
        proc::syscall_prototype::SetgidHandler, proc::syscall_prototype::SetegidHandler,
        proc::syscall_prototype::SetreuidHandler,
        proc::syscall_prototype::SetregidHandler,
        proc::syscall_prototype::GetgroupsHandler,
        proc::syscall_prototype::SetgroupsHandler,
        proc::syscall_prototype::ProcessSnapshotHandler,
        proc::syscall_prototype::GetsidHandler, fs::syscall_prototype::UnmountHandler,
        fs::syscall_prototype::MountHandler, proc::syscall_prototype::GetrlimitHandler,
        proc::syscall_prototype::SetrlimitHandler,
        proc::syscall_prototype::SigwaitHandler,
        proc::syscall_prototype::SigaltstackHandler,
        proc::syscall_prototype::GetpgidHandler, proc::syscall_prototype::GetppidHandler,
        proc::syscall_prototype::ThreadCancelHandler,
        proc::syscall_prototype::AlarmHandler,
        proc::syscall_prototype::ThreadCreateOwnedHandler,
        proc::syscall_prototype::ThreadCancelPendingHandler,
        proc::syscall_prototype::ThreadExistsHandler,
        proc::syscall_prototype::ThreadKillHandler,
        proc::syscall_prototype::SigpendingHandler,
        proc::syscall_prototype::SigtimedwaitHandler,
        sys::syscall_prototype::ClockGetresHandler,
        proc::syscall_prototype::ThreadGetattrHandler,
        mm::syscall_prototype::MsyncHandler, fs::syscall_prototype::LseekHandler,
        fs::syscall_prototype::UmaskHandler, fs::syscall_prototype::ShmOpenHandler,
        fs::syscall_prototype::ShmUnlinkHandler, fs::syscall_prototype::FchdirHandler,
        net::syscall_prototype::AcceptHandler, net::syscall_prototype::BindHandler,
        net::syscall_prototype::ConnectHandler, net::syscall_prototype::GetsockoptHandler,
        net::syscall_prototype::ListenHandler, net::syscall_prototype::RecvHandler,
        net::syscall_prototype::RecvfromHandler, net::syscall_prototype::SendHandler,
        net::syscall_prototype::SendtoHandler, net::syscall_prototype::ShutdownHandler,
        net::syscall_prototype::SocketHandler, net::syscall_prototype::SocketpairHandler>;
}

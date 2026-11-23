#pragma once

#include "syscall.hpp"

namespace astia::fs::syscalls {
    extern const ::astia::sys::syscall_descriptor io_setup_descriptor;
}
namespace astia::fs::syscalls {
    extern const ::astia::sys::syscall_descriptor io_destroy_descriptor;
}
namespace astia::fs::syscalls {
    extern const ::astia::sys::syscall_descriptor io_submit_descriptor;
}
namespace astia::fs::syscalls {
    extern const ::astia::sys::syscall_descriptor io_register_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor mmap_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor munmap_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor mprotect_descriptor;
}
namespace astia::sys::syscalls {
    extern const ::astia::sys::syscall_descriptor clock_gettime_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor exit_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor yield_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor getpid_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor gettid_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor futex_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor waitpid_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor spawnve_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor fork_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor thread_create_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor thread_create2_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor thread_exit_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor thread_join_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor thread_detach_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor kill_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor sigaction_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor sigprocmask_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor execve_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor sigsuspend_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor getpgrp_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor setpgid_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor setsid_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor sigreturn_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor chdir_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor getcwd_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor getuid_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor geteuid_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor getgid_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor getegid_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor setuid_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor seteuid_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor setgid_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor setegid_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor setreuid_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor setregid_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor getgroups_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor setgroups_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor process_snapshot_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor getsid_descriptor;
}
namespace astia::fs::syscalls {
    extern const ::astia::sys::syscall_descriptor unmount_descriptor;
}
namespace astia::fs::syscalls {
    extern const ::astia::sys::syscall_descriptor mount_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor getrlimit_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor setrlimit_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor sigwait_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor sigaltstack_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor getpgid_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor getppid_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor thread_cancel_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor alarm_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor thread_create_owned_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor thread_cancel_pending_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor thread_exists_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor thread_kill_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor sigpending_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor sigtimedwait_descriptor;
}
namespace astia::sys::syscalls {
    extern const ::astia::sys::syscall_descriptor clock_getres_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor thread_getattr_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor msync_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor lseek_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor umask_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor shm_open_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor shm_unlink_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor fchdir_descriptor;
}
namespace astia::proc::syscalls {
    extern const ::astia::sys::syscall_descriptor uname_descriptor;
}

namespace astia::sys {
    template <usize Count>
    consteval bool valid_syscall_id_manifest(const u32 (&ids)[Count], u32 first,
                                             u32 last) {
        if (Count != static_cast<usize>(last - first + 1))
            return false;
        for (usize i = 0; i < Count; ++i) {
            if (ids[i] != first + i)
                return false;
        }
        return true;
    }
    namespace production {
        inline constexpr u32 descriptor_ids[] = {
            0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17,
            18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35,
            36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53,
            54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70};
        static_assert(valid_syscall_id_manifest(descriptor_ids, 0, 70));
        // Address constants keep this table statically initialized; the
        // freestanding kernel runs no dynamic initializers for value copies.
        inline constexpr const syscall_descriptor* const descriptors[] = {
            &::astia::fs::syscalls::io_setup_descriptor,
            &::astia::fs::syscalls::io_destroy_descriptor,
            &::astia::fs::syscalls::io_submit_descriptor,
            &::astia::fs::syscalls::io_register_descriptor,
            &::astia::proc::syscalls::mmap_descriptor,
            &::astia::proc::syscalls::munmap_descriptor,
            &::astia::proc::syscalls::mprotect_descriptor,
            &::astia::sys::syscalls::clock_gettime_descriptor,
            &::astia::proc::syscalls::exit_descriptor,
            &::astia::proc::syscalls::yield_descriptor,
            &::astia::proc::syscalls::getpid_descriptor,
            &::astia::proc::syscalls::gettid_descriptor,
            &::astia::proc::syscalls::futex_descriptor,
            &::astia::proc::syscalls::waitpid_descriptor,
            &::astia::proc::syscalls::spawnve_descriptor,
            &::astia::proc::syscalls::fork_descriptor,
            &::astia::proc::syscalls::thread_create_descriptor,
            &::astia::proc::syscalls::thread_exit_descriptor,
            &::astia::proc::syscalls::thread_join_descriptor,
            &::astia::proc::syscalls::thread_detach_descriptor,
            &::astia::proc::syscalls::kill_descriptor,
            &::astia::proc::syscalls::sigaction_descriptor,
            &::astia::proc::syscalls::sigprocmask_descriptor,
            &::astia::proc::syscalls::thread_create2_descriptor,
            &::astia::proc::syscalls::execve_descriptor,
            &::astia::proc::syscalls::sigsuspend_descriptor,
            &::astia::proc::syscalls::getpgrp_descriptor,
            &::astia::proc::syscalls::setpgid_descriptor,
            &::astia::proc::syscalls::setsid_descriptor,
            &::astia::proc::syscalls::sigreturn_descriptor,
            &::astia::proc::syscalls::chdir_descriptor,
            &::astia::proc::syscalls::getcwd_descriptor,
            &::astia::proc::syscalls::getuid_descriptor,
            &::astia::proc::syscalls::geteuid_descriptor,
            &::astia::proc::syscalls::getgid_descriptor,
            &::astia::proc::syscalls::getegid_descriptor,
            &::astia::proc::syscalls::setuid_descriptor,
            &::astia::proc::syscalls::seteuid_descriptor,
            &::astia::proc::syscalls::setgid_descriptor,
            &::astia::proc::syscalls::setegid_descriptor,
            &::astia::proc::syscalls::setreuid_descriptor,
            &::astia::proc::syscalls::setregid_descriptor,
            &::astia::proc::syscalls::getgroups_descriptor,
            &::astia::proc::syscalls::setgroups_descriptor,
            &::astia::proc::syscalls::process_snapshot_descriptor,
            &::astia::proc::syscalls::getsid_descriptor,
            &::astia::fs::syscalls::unmount_descriptor,
            &::astia::fs::syscalls::mount_descriptor,
            &::astia::proc::syscalls::getrlimit_descriptor,
            &::astia::proc::syscalls::setrlimit_descriptor,
            &::astia::proc::syscalls::sigwait_descriptor,
            &::astia::proc::syscalls::sigaltstack_descriptor,
            &::astia::proc::syscalls::getpgid_descriptor,
            &::astia::proc::syscalls::getppid_descriptor,
            &::astia::proc::syscalls::thread_cancel_descriptor,
            &::astia::proc::syscalls::alarm_descriptor,
            &::astia::proc::syscalls::thread_create_owned_descriptor,
            &::astia::proc::syscalls::thread_cancel_pending_descriptor,
            &::astia::proc::syscalls::thread_exists_descriptor,
            &::astia::proc::syscalls::thread_kill_descriptor,
            &::astia::proc::syscalls::sigpending_descriptor,
            &::astia::proc::syscalls::sigtimedwait_descriptor,
            &::astia::sys::syscalls::clock_getres_descriptor,
            &::astia::proc::syscalls::thread_getattr_descriptor,
            &::astia::proc::syscalls::msync_descriptor,
            &::astia::proc::syscalls::lseek_descriptor,
            &::astia::proc::syscalls::umask_descriptor,
            &::astia::proc::syscalls::shm_open_descriptor,
            &::astia::proc::syscalls::shm_unlink_descriptor,
            &::astia::proc::syscalls::fchdir_descriptor,
            &::astia::proc::syscalls::uname_descriptor};
        static_assert(sizeof(descriptors) / sizeof(descriptors[0]) == 71);
    } // namespace production
} // namespace astia::sys

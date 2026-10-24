#include "syscalls/io_setup.hpp"
#include "syscalls/io_destroy.hpp"
#include "syscalls/io_enter.hpp"
#include "syscalls/io_register.hpp"
#include "syscall.hpp"

namespace astia::fs {

    bool register_syscalls() {
        return sys::register_syscall<syscall::io_setup>(sys::syscall_id::io_setup) &&
               sys::register_syscall<syscall::io_destroy>(sys::syscall_id::io_destroy) &&
               sys::register_syscall<syscall::io_enter>(sys::syscall_id::io_enter) &&
               sys::register_syscall<syscall::io_register>(sys::syscall_id::io_register);
    }

} // namespace astia::fs

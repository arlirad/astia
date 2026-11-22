#include <astia/api/errno.h>
#include <astia/api/syscalls.h>
#include <astia/arch/platform.hpp>
#include <astia/version.h>

#include "../../proc.hpp"
#include "../../utsname.hpp"
#include "user_mem.hpp"

#include <stddef.h>
#include <syscall.hpp>

namespace astia::proc::syscalls {

    /// uname - Read the system identity.
    ///
    /// Synopsis:
    ///     long astia_uname(astia_utsname *output);
    ///
    /// Description:
    ///     Copies the kernel's system identification into output. The five
    ///     fields are fixed 65-byte arrays; every byte after a field's
    ///     terminating NUL is zero.
    ///
    /// Return Value:
    ///     Returns zero on success or a negative errno.
    ///
    /// Errors:
    ///     -EFAULT: output is null or not writable userspace memory.
    ///     -ENOSYS: The selected architecture has no machine identity.
    ///     -ESRCH: There is no current process.
    struct uname
        : sys::syscall_handler<uname, sys::syscall_id::uname, sys::syscall_mode::sync> {
        static_assert(sizeof(astia_utsname) == 325);
        static_assert(offsetof(astia_utsname, sysname) == 0);
        static_assert(offsetof(astia_utsname, nodename) == 65);
        static_assert(offsetof(astia_utsname, release) == 130);
        static_assert(offsetof(astia_utsname, version) == 195);
        static_assert(offsetof(astia_utsname, machine) == 260);

        static i64 invoke(sys::user_ptr<astia_utsname> output) {
            if (current_process() == nullptr)
                return -ESRCH;
            const char*   machine = arch::machine_name();
            astia_utsname value{};
            if (machine == nullptr ||
                !proc::fill_utsname(value, ASTIA_VERSION_STRING,
                                    "Astia " ASTIA_VERSION_STRING, machine))
                return -ENOSYS;
            const auto validated = output.validate();
            if (!validated || !sys::copy_out(validated.value.bytes(), &value))
                return -EFAULT;
            return 0;
        }
    };

    extern const sys::syscall_descriptor uname_descriptor = uname::descriptor;

} // namespace astia::proc::syscalls

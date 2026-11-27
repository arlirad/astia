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
} // namespace astia::proc::syscalls

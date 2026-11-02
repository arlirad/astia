#include "spawnve.hpp"

#include "../../../ipc/futex.hpp"
#include "../../../ipc/mapping.hpp"
#include "../../mapping.hpp"
#include "../../proc.hpp"
#include "../../scheduler.hpp"
#include "argv.hpp"
#include "syscall.hpp"
#include "user_mem.hpp"
#include <astia/api/aio.h>
#include <astia/api/errno.h>
#include <astia/api/syscalls.h>
#include <astia/arch/context.hpp>
#include <astia/arch/cpu.hpp>
#include <astia/fs.hpp>
#include <astia/log.hpp>
namespace astia::proc::syscall {
    using namespace detail;
    i64 spawnve(u64 path_ptr, u64 argv_ptr, u64 envp_ptr) {
        char path[path_cap];
        if (!sys::copy_in_string(span<char>(path, sizeof path),
                                 sys::user_ptr<char>(path_ptr))) {
            return -EFAULT;
        }

        argument_staging staging;
        if (!staging.allocate()) {
            return -ENOMEM;
        }
        i64 result = staging.copy(argv_ptr, envp_ptr);
        if (result == 0) {
            auto child = proc::spawnve(path, staging.argv(), staging.envp());
            result     = child ? static_cast<i64>(raw_id(child.value->pid()))
                               : -astia::errno_value(child.error);
        }
        return result;
    }
}

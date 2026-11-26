#include <astia/api/aio.h>
#include <astia/api/errno.h>
#include <astia/api/syscalls.h>
#include <astia/arch/context.hpp>
#include <astia/arch/cpu.hpp>
#include <astia/fs.hpp>
#include <astia/log.hpp>
#include <astia/mm/heap.hpp>

#include "../../../ipc/futex.hpp"
#include "../../../ipc/mapping.hpp"
#include "../../mapping.hpp"
#include "../../proc.hpp"
#include "../../scheduler.hpp"
#include "syscall.hpp"
#include "user_mem.hpp"

#include <syscall.hpp>
namespace astia::proc::syscalls {
    struct fork
        : sys::syscall_handler<fork, sys::syscall_id::fork, sys::syscall_mode::sync> {
        static i64 invoke(sys::syscall_trap trap) {
            auto* owner = current_process();
            if (owner == nullptr)
                return -ESRCH;

            const u64(&arguments)[sys::syscall_argument_count] = trap.arguments;

            return proc::fork(arch::caller_image(trap.image, arguments[0], arguments[1],
                                                 arguments[2], arguments[3], arguments[4],
                                                 arguments[5], owner->tls_tp()));
        }
    };
} // namespace astia::proc::syscalls

#include "thread_exit.hpp"

#include "../../../ipc/futex.hpp"
#include "../../../ipc/mapping.hpp"
#include "../../mapping.hpp"
#include "../../proc.hpp"
#include "../../scheduler.hpp"
#include "syscall.hpp"
#include "user_mem.hpp"
#include <astia/api/aio.h>
#include <astia/api/errno.h>
#include <astia/api/syscalls.h>
#include <astia/arch/context.hpp>
#include <astia/arch/cpu.hpp>
#include <astia/fs.hpp>
#include <astia/log.hpp>
#include <astia/mm/heap.hpp>
namespace astia::proc::syscall {

    i64 thread_exit(i64 result) {
        auto* self = current_thread();
        if (self == nullptr) {
            return -ESRCH;
        }
        self->exit(reinterpret_cast<void*>(static_cast<usize>(result)));
        __builtin_unreachable();
    }
}

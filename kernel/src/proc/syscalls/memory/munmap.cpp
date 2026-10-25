#include "munmap.hpp"
#include "../../proc.hpp"
#include "../../mapping.hpp"
#include "../../scheduler.hpp"
#include "user_mem.hpp"
#include "syscall.hpp"
#include "../../../ipc/futex.hpp"
#include "../../../ipc/mapping.hpp"
#include <astia/api/aio.h>
#include <astia/api/errno.h>
#include <astia/api/syscalls.h>
#include <astia/arch/context.hpp>
#include <astia/arch/cpu.hpp>
#include <astia/fs.hpp>
#include <astia/log.hpp>
#include <astia/mm/heap.hpp>
namespace astia::proc::syscall {
        i64 munmap(u64 addr, i64 len) {
            auto* process = current_process();
            if (process == nullptr) {
                return -ESRCH;
            }
            if (len <= 0) {
                return -EINVAL;
            }
            return process->unmap(addr, static_cast<usize>(len));
        }
}

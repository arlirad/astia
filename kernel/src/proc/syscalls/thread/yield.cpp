#include "yield.hpp"
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

        i64 yield() {
            scheduler::yield();
            return 0;
        }
}

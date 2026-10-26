#include "spawnve.hpp"
#include "argv.hpp"
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
    using namespace detail;
        i64 spawnve(u64 path_ptr, u64 argv_ptr, u64 envp_ptr) {
            char path[path_cap];
            if (!sys::copy_in_string(path, sizeof path,
                                     sys::user_range::string(path_ptr))) {
                return -EFAULT;
            }

            // One heap block: both pointer vectors, then the string
            // pool behind them — the kernel stack must not carry up to
            // 16 KB of copied arguments through the ELF-loading call
            // chain that follows.
            const usize vec_bytes  = 2 * (vec_max + 1) * sizeof(char*);
            const usize pool_bytes = 2 * vec_max * arg_cap;
            auto        storage    = mm::heap::alloc(vec_bytes + pool_bytes);
            if (!storage) {
                return -ENOMEM;
            }
            char** argv_vec = reinterpret_cast<char**>(storage.value);
            char** envp_vec = argv_vec + vec_max + 1;
            char*  pool     = reinterpret_cast<char*>(envp_vec + vec_max + 1);

            i64 result = copy_in_vector(argv_vec, pool, argv_ptr);
            if (result == 0) {
                result = copy_in_vector(envp_vec, pool + vec_max * arg_cap, envp_ptr);
            }
            if (result == 0) {
                auto child = proc::spawnve(path, argv_vec, envp_vec);
                result = child ? static_cast<i64>(static_cast<u32>(child.value->pid()))
                               : -astia::errno_value(child.error);
            }
            mm::heap::free(storage.value);
            return result;
        }
}

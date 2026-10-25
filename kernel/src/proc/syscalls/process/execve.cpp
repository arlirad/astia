#include "execve.hpp"
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

        i64 execve(const arch::syscall_frame& frame) {
            using exec_logs      = astia::log::context<"execve">;
            auto*     exec_owner = current_process();
            const u64 path_ptr   = frame.a0;
            const u64 argv_ptr   = frame.a1;
            const u64 envp_ptr   = frame.a2;
            char      path[path_cap];
            if (!sys::copy_in_string(path, sizeof path,
                                     sys::user_range::string(path_ptr)))
                return -EFAULT;
            const usize vec_bytes  = 2 * (vec_max + 1) * sizeof(char*);
            const usize pool_bytes = 2 * vec_max * arg_cap;
            auto        storage    = mm::heap::alloc(vec_bytes + pool_bytes);
            if (!storage)
                return -ENOMEM;
            char** argv_vec = reinterpret_cast<char**>(storage.value);
            char** envp_vec = argv_vec + vec_max + 1;
            char*  pool     = reinterpret_cast<char*>(envp_vec + vec_max + 1);
            i64    result   = copy_in_vector(argv_vec, pool, argv_ptr);
            if (result == 0)
                result = copy_in_vector(envp_vec, pool + vec_max * arg_cap, envp_ptr);
            if (result == 0) {
                exec_logs::info("request pid=%d path=%s argv0=%s argv1=%s",
                                exec_owner ? static_cast<int>(exec_owner->pid()) : -1,
                                path, argv_vec[0] ? argv_vec[0] : "(null)",
                                argv_vec[1] ? argv_vec[1] : "(null)");
            }
            if (result == 0) {
                auto* owner = current_process();
                auto  file  = fs::open_path(path);
                if (owner == nullptr || !file)
                    result = -ENOENT;
                else {
                    const long size  = file->size();
                    u64        entry = 0, stack = 0;
                    if (size <= 0 ||
                        !owner->exec_user_file(file, static_cast<usize>(size), argv_vec,
                                               envp_vec, &entry, &stack)) {
                        exec_logs::error("load failed path=%s size=%ld", path, size);
                        result = -ENOEXEC;
                    }
                    else {
                        exec_logs::info("loaded path=%s entry=%llx stack=%llx tls=%llx",
                                        path, static_cast<unsigned long long>(entry),
                                        static_cast<unsigned long long>(stack),
                                        static_cast<unsigned long long>(owner->tls_tp()));
                        auto& next  = const_cast<arch::syscall_frame&>(frame);
                        next.rip    = entry;
                        next.rsp    = stack;
                        next.rflags = 0x202;
                        arch::context_set_fs_base(current_thread()->context(),
                                                  owner->tls_tp());
                        result = 0;
                    }
                }
            }
            mm::heap::free(storage.value);
            return result;
        }
}

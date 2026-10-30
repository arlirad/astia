#include "execve.hpp"

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
#include <astia/mm/heap.hpp>
namespace astia::proc::syscall {
    using namespace detail;

    i64 execve(const arch::syscall_frame& frame) {
        // The syscall frame is the return context of this thread.  Exec
        // replaces the address space and rewrites that frame; do not let
        // the scheduler snapshot the half-replaced context in between.
        arch::interrupt_guard gate;
        const u64             path_ptr = frame.a0;
        const u64             argv_ptr = frame.a1;
        const u64             envp_ptr = frame.a2;
        char                  path[path_cap];
        if (!sys::copy_in_string(path, sizeof path, sys::user_range::string(path_ptr)))
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
                    result = -ENOEXEC;
                }
                else {
                    // This is the existing thread, not a freshly
                    // prepared spawn thread.  Update both halves of the
                    // ABI state: the syscall iret image and the thread
                    // bookkeeping consumed by a subsequent fork or
                    // context hand-off.
                    current_thread()->user_stack_top(stack);
                    arch::context_set_fs_base(current_thread()->context(),
                                              owner->tls_tp());
                    // Write the live syscall iret image last.  The exec
                    // loader may run with interrupts enabled and a
                    // context hand-off can otherwise leave the return
                    // image stale while the thread bookkeeping is being
                    // updated.
                    auto& next  = const_cast<arch::syscall_frame&>(frame);
                    next.rip    = entry;
                    next.rsp    = stack;
                    next.rflags = 0x202;
                    result      = 0;
                }
            }
        }
        mm::heap::free(storage.value);
        return result;
    }
}

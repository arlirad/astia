#include "proc.hpp"
#include "share.hpp"
#include <astia/api/errno.h>

namespace astia::proc {

    auto fork(const arch::user_context* uctx) -> i64 {
        process* parent = current_process();
        if (parent == nullptr || uctx == nullptr) {
            return -ESRCH; // kernel threads have nothing to duplicate
        }
        if (parent->address_space() == nullptr) {
            return -EAGAIN;
        }

        // The arch entry packs registers but cannot know the process's
        // thread pointer — the address space owns it.
        arch::user_context image = *uctx;
        image.fs_base            = parent->tls_tp();

        auto child = mm::share<process>(allocate_pid(), parent->pid(), parent->name());
        if (!child) {
            return -ENOMEM;
        }
        if (!child->copy_descriptors_from(*parent) ||
            !parent->clone_address_space_cow(child.get()) ||
            !child->create_fork_thread(image)) {
            // The child shptr drops here; ~process releases every
            // backing reference and descriptor its failed clone took.
            return -EAGAIN;
        }
        if (!register_process(child)) {
            return -EAGAIN;
        }
        child->state(process_state::runnable);
        return static_cast<i64>(child->pid());
    }

} // namespace astia::proc

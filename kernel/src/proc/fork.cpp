#include "proc.hpp"
#include <astia/api/errno.h>
#include <astia/mm/share.hpp>

namespace astia::proc {

    i64 fork(const arch::user_context& uctx) {
        process* parent = current_process();
        if (parent == nullptr) {
            return -ESRCH; // kernel threads have nothing to duplicate
        }
        if (parent->address_space() == nullptr) {
            return -EAGAIN;
        }

        // The trap image carries registers but not the thread pointer —
        // the address space owns that, so the child's copy of the image
        // gains it here.
        arch::user_context child_image = uctx;
        child_image.fs_base            = parent->tls_tp();

        auto child = mm::share<process>(allocate_pid(), parent->pid(), parent->name());
        if (!child) {
            return -ENOMEM;
        }
        child->inherit_groups(*parent);
        if (!child->copy_descriptors_from(*parent) ||
            !parent->clone_address_space_cow(child.get()) ||
            !child->create_fork_thread(child_image)) {
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

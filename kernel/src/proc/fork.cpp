#include "proc.hpp"
#include <astia/api/errno.h>
#include <astia/mm/share.hpp>

namespace astia::proc {

    namespace {
        bool valid_inherited_stack_metadata(const thread* value, u64& base,
                                            usize& size) {
            if (value == nullptr || !value->user_stack_metadata_supported())
                return false;
            base = value->user_stack_base();
            size = value->user_stack_size();
            const u64 bytes = static_cast<u64>(size);
            return base != 0 && bytes != 0 && bytes <= ~u64{0} - base &&
                   base + bytes <= user_address_limit &&
                   (base & thread_stack_alignment_mask) == 0;
        }
    }

    i64 fork(const arch::user_context& uctx) {
        process* parent = current_process();
        if (parent == nullptr) {
            return -ESRCH; // kernel threads have nothing to duplicate
        }
        if (parent->address_space() == nullptr) {
            return -EAGAIN;
        }
        thread* parent_thread = current_thread();
        if (parent_thread == nullptr) {
            return -ESRCH;
        }

        u64   stack_base = 0;
        usize stack_size = 0;
        if (!valid_inherited_stack_metadata(parent_thread, stack_base, stack_size)) {
            stack_base = 0;
            stack_size = 0;
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
        child->signals().inherit_dispositions_from(parent->signals());
        // create_fork_thread() queues the child immediately.  Publish its
        // initial state before that queue entry can run.
        child->state(process_state::runnable);
        if (!child->copy_descriptors_from(*parent) ||
            !parent->clone_address_space_cow(child.get()) || !register_process(child)) {
            // The child shptr drops here; ~process releases every
            // backing reference and descriptor its failed clone took.
            return -EAGAIN;
        }
        if (!child->create_fork_thread(child_image, parent_thread->signals().blocked(),
                                       stack_base, stack_size)) {
            (void) unregister_process(child.get());
            parent->wake_waiters();
            return -EAGAIN;
        }
        return static_cast<i64>(raw_id(child->pid()));
    }

} // namespace astia::proc

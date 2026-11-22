#include "common.hpp"

namespace astia::io::aio::operations {

    struct dup2 : handler<dup2, opcode::dup2> {
        static bool invoke([[maybe_unused]] ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
            proc::process* current = proc::current_process();
            auto           old_fd  = mapper<fd_t>::from(sqe.fd);
            auto           new_fd  = mapper<fd_t>::from(static_cast<i64>(sqe.offset));
            if (current == nullptr || !old_fd.has_value || !new_fd.has_value ||
                !current->descriptor(old_fd.value)) {
                return cq_post_guarded(ring, sqe.user_data, -EBADF);
            }
            const i32 target = new_fd.value.value;
            if (target != sqe.fd) {
                const auto result =
                    current->duplicate_descriptor_to(old_fd.value, new_fd.value);
                if (!result)
                    return cq_post_guarded(ring, sqe.user_data, -EMFILE);
            }
            return cq_post_guarded(ring, sqe.user_data, target);
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor dup2_descriptor = dup2::descriptor;

} // namespace astia::io::aio::operations

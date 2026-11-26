#include "common.hpp"

namespace astia::io::aio::operations {
    struct dup2 : handler<dup2, opcode::dup2> {
        static core::task<aio_result>
        invoke([[maybe_unused]] ring::ring_slot& slot, [[maybe_unused]] astia_rings* ring,
               astia_sqe sqe, [[maybe_unused]] core::cltoken token) noexcept {
            proc::process* current = proc::current_process();
            auto           old_fd  = mapper<fd_t>::from(sqe.fd);
            auto           new_fd  = mapper<fd_t>::from(static_cast<i64>(sqe.offset));

            if (current == nullptr || !old_fd.has_value || !new_fd.has_value ||
                !current->descriptor(old_fd.value))
                co_return -EBADF;

            const i32 target = new_fd.value.value;

            if (target != sqe.fd) {
                const auto result =
                    current->duplicate_descriptor_to(old_fd.value, new_fd.value);
                if (!result)
                    co_return -EMFILE;
            }

            co_return target;
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor dup2_descriptor = dup2::descriptor;
} // namespace astia::io::aio::operations

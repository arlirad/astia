#include "../../io/aio/handler.hpp"
#include "common.hpp"

namespace astia::io::aio::operations {
    struct chmod : handler<chmod, opcode::chmod> {
        static core::task<aio_result>
        invoke([[maybe_unused]] ring::ring_slot& slot, [[maybe_unused]] astia_rings* ring,
               astia_sqe sqe, [[maybe_unused]] core::cltoken token) noexcept {
            auto* current = proc::current_process();
            if (current == nullptr)
                co_return -ESRCH;

            auto requested = mapper<fs::file_permissions>::from(
                sqe.address == 0 ? sqe.aux & fs::file_permissions_mask : sqe.aux);

            if (!requested.has_value)
                co_return -EINVAL;

            if (sqe.address == 0) {
                auto mapped_fd = mapper<fd_t>::from(sqe.fd);

                if (!mapped_fd.has_value)
                    co_return -EBADF;

                auto handle = current->descriptor(mapped_fd.value);
                if (!handle)
                    co_return -EBADF;

                if (!handle->parent())
                    co_return -ENOSYS;

                if (current->euid() != 0 &&
                    current->euid() != static_cast<u32>(handle->parent()->uid()))
                    co_return -EPERM;

                co_return static_cast<i32>(handle->chmod(requested.value));
            }

            char path[256];

            if (!memory::copy_in_string(span<char>(path, sizeof path),
                                        memory::user_ptr<char>(sqe.address)))
                co_return -EFAULT;

            char       resolved[256];
            const long resolve_rc =
                resolve_path_for(current, path, resolved, sizeof resolved, true);

            if (resolve_rc < 0)
                co_return (i32) resolve_rc;

            if (current->euid() != 0 &&
                current->euid() != static_cast<u32>(fs::node_uid(resolved)))
                co_return -EPERM;

            co_return static_cast<i32>(fs::chmod_path(resolved, requested.value));
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor chmod_descriptor = chmod::descriptor;
} // namespace astia::io::aio::operations

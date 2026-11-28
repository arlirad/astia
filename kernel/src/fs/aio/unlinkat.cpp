#include "../../io/aio/handler.hpp"
#include "../events.hpp"
#include "common.hpp"

namespace astia::io::aio::operations {
    struct unlinkat : handler<unlinkat, opcode::unlinkat> {
        static core::task<aio_result>
        invoke([[maybe_unused]] ring::ring_slot& slot, [[maybe_unused]] astia_rings* ring,
               astia_sqe sqe, [[maybe_unused]] core::cltoken token) noexcept {
            char path[256];

            if (!memory::copy_in_string(span<char>(path, sizeof path),
                                        memory::user_ptr<char>(sqe.address)))
                co_return -EFAULT;

            auto* current = proc::current_process();

            char resolved[256];

            if (current == nullptr)
                co_return -ESRCH;

            long resolve_rc =
                resolve_at(current, sqe.fd, path, resolved, sizeof resolved, false);
            if (resolve_rc < 0)
                co_return (i32) resolve_rc;

            // The identity is captured once, from the resolution the checks
            // below use.  The removal must bind this same node; a name that
            // changed after the checks fails with -EAGAIN.
            const fs::node_identity checked = fs::node_identity_of(resolved);

            if (!parent_writable(current, resolved))
                co_return -EACCES;

            if (!sticky_directory_allows(current, resolved))
                co_return -EPERM;

            fs::events::unlink_checked::emit(resolved);

            const long rc = fs::remove_path_verified(
                resolved, (sqe.op_flags & UNLINKAT_REMOVEDIR) != 0, checked);

            co_return static_cast<i32>(rc);
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor unlinkat_descriptor = unlinkat::descriptor;
} // namespace astia::io::aio::operations

#include "../../io/aio/handler.hpp"
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
            char  resolved[256];
            if (current == nullptr)
                co_return -ESRCH;
            long resolve_rc =
                resolve_at(current, sqe.fd, path, resolved, sizeof resolved);
            if (resolve_rc >= 0)
                resolve_rc = fs::resolve_path(resolved, resolved, sizeof resolved, false);
            if (resolve_rc < 0)
                co_return (i32) resolve_rc;
            const long rc =
                fs::remove_path(resolved, (sqe.op_flags & UNLINKAT_REMOVEDIR) != 0);
            co_return static_cast<i32>(rc);
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor unlinkat_descriptor = unlinkat::descriptor;

} // namespace astia::io::aio::operations

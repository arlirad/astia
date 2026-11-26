#include "../../io/aio/handler.hpp"
#include "common.hpp"

namespace astia::io::aio::operations {
    struct symlinkat : handler<symlinkat, opcode::symlinkat> {
        static core::task<aio_result>
        invoke([[maybe_unused]] ring::ring_slot& slot, [[maybe_unused]] astia_rings* ring,
               astia_sqe sqe, [[maybe_unused]] core::cltoken token) noexcept {
            char target[256], path[256];

            if (!memory::copy_in_string(span<char>(target, sizeof target),
                                        memory::user_ptr<char>(sqe.address)) ||
                !memory::copy_in_string(span<char>(path, sizeof path),
                                        memory::user_ptr<char>(sqe.offset)))
                co_return -EFAULT;

            auto* current = proc::current_process();

            char resolved[256];

            if (!current)
                co_return -ESRCH;

            long resolve_rc =
                resolve_at(current, sqe.fd, path, resolved, sizeof resolved, false);
            if (resolve_rc < 0)
                co_return (i32) resolve_rc;

            long link_rc = fs::make_symlink(target, resolved);
            if (link_rc >= 0)
                (void) fs::chown_path(resolved, uid_t{current->uid()},
                                      gid_t{current->gid()}, false);

            co_return (i32) link_rc;
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor symlinkat_descriptor = symlinkat::descriptor;
} // namespace astia::io::aio::operations

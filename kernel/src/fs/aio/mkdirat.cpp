#include "../../io/aio/handler.hpp"
#include "common.hpp"

namespace astia::io::aio::operations {
    struct mkdirat : handler<mkdirat, opcode::mkdirat> {
        static core::task<aio_result>
        invoke([[maybe_unused]] ring::ring_slot& slot, [[maybe_unused]] astia_rings* ring,
               astia_sqe sqe, [[maybe_unused]] core::cltoken token) noexcept {
            char path[256];

            if (!memory::copy_in_string(span<char>(path, sizeof path),
                                        memory::user_ptr<char>(sqe.address)))
                co_return -EFAULT;

            usz length = 0;

            while (path[length] != '\0') {
                ++length;
            }

            while (length > 1 && path[length - 1] == '/') {
                path[--length] = '\0';
            }

            auto* current = proc::current_process();

            char resolved[256];

            if (current == nullptr)
                co_return -ESRCH;

            long resolve_rc =
                resolve_at(current, sqe.fd, path, resolved, sizeof resolved, false);
            if (resolve_rc < 0)
                co_return (i32) resolve_rc;

            if (fs::node_type(resolved) == fs::directory_lnk)
                co_return -EEXIST;

            if (!parent_writable(current, resolved))
                co_return -EACCES;

            const auto requested = mapper<fs::file_mode>::from(
                sqe.aux == 0 ? fs::default_directory_permissions.raw() : sqe.aux);

            if (!requested.has_value)
                co_return -EINVAL;

            const long rc = fs::make_directory(
                resolved, fs::apply_umask(requested.value, current->umask()));
            if (rc >= 0)
                (void) fs::chown_path(resolved, uid_t{current->uid()},
                                      gid_t{current->gid()});

            co_return static_cast<i32>(rc);
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor mkdirat_descriptor = mkdirat::descriptor;
} // namespace astia::io::aio::operations

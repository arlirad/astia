#include "../../io/aio/handler.hpp"
#ifdef ASTIA_AIO_HOST_TEST
#include "dispatch.hpp"
#else
#include "common.hpp"
#endif

namespace astia::io::aio::operations {

#ifndef ASTIA_AIO_HOST_TEST
    namespace {

        bool handle_mkdirat(ring_slot&, astia_rings* ring, const astia_sqe& s) {
            char path[256];
            if (!memory::copy_in_string(span<char>(path, sizeof path),
                                        memory::user_ptr<char>(s.address)))
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            auto* current = proc::current_process();
            char  resolved[256];
            if (current == nullptr)
                return cq_post_guarded(ring, s.user_data, -ESRCH);
            long resolve_rc = resolve_at(current, s.fd, path, resolved, sizeof resolved);
            if (resolve_rc >= 0)
                resolve_rc = fs::resolve_path(resolved, resolved, sizeof resolved, true);
            if (resolve_rc < 0)
                return cq_post_guarded(ring, s.user_data, (i32) resolve_rc);
            auto requested = mapper<fs::file_mode>::from(
                s.aux == 0 ? fs::default_directory_permissions.raw() : s.aux);
            if (!requested.has_value)
                return cq_post_guarded(ring, s.user_data, -EINVAL);
            const long rc = fs::make_directory(resolved, requested.value);
            if (rc >= 0)
                (void) fs::chown_path(resolved, uid_t{current->uid()},
                                      gid_t{current->gid()});
            return cq_post_guarded(ring, s.user_data, static_cast<i32>(rc));
        }

    } // namespace
#endif

    struct mkdirat : handler<mkdirat, opcode::mkdirat> {
        static bool invoke([[maybe_unused]] ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
#ifdef ASTIA_AIO_HOST_TEST
            return ::astia::io::aio::filesystem::dispatch(slot, ring, sqe);
#else
            return handle_mkdirat(slot, ring, sqe);
#endif
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor mkdirat_descriptor = mkdirat::descriptor;

} // namespace astia::io::aio::operations

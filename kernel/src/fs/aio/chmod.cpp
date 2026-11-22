#include "../../io/aio/handler.hpp"
#ifdef ASTIA_AIO_HOST_TEST
#include "dispatch.hpp"
#else
#include "common.hpp"
#endif

namespace astia::io::aio::operations {

#ifndef ASTIA_AIO_HOST_TEST
    namespace {

        bool handle_chmod(astia_rings* ring, const astia_sqe& s) {
            auto* current = proc::current_process();
            if (current == nullptr)
                return cq_post_guarded(ring, s.user_data, -ESRCH);
            auto requested = mapper<fs::file_permissions>::from(
                s.address == 0 ? s.aux & fs::file_permissions_mask : s.aux);
            if (!requested.has_value)
                return cq_post_guarded(ring, s.user_data, -EINVAL);
            if (s.address == 0) {
                auto mapped_fd = mapper<fd_t>::from(s.fd);
                if (!mapped_fd.has_value)
                    return cq_post_guarded(ring, s.user_data, -EBADF);
                auto object = current->descriptor(mapped_fd.value);
                if (!object)
                    return cq_post_guarded(ring, s.user_data, -EBADF);
                if (!object->node())
                    return cq_post_guarded(ring, s.user_data, -ENOSYS);
                if (current->euid() != 0 &&
                    current->euid() != static_cast<u32>(object->node()->uid()))
                    return cq_post_guarded(ring, s.user_data, -EPERM);
                return cq_post_guarded(ring, s.user_data,
                                       static_cast<i32>(object->chmod(requested.value)));
            }
            char path[256];
            if (!memory::copy_in_string(span<char>(path, sizeof path),
                                        memory::user_ptr<char>(s.address)))
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            char resolved[256];
            if (current == nullptr ||
                current->compose_path(path, resolved, sizeof resolved) < 0)
                return cq_post_guarded(ring, s.user_data, -ESRCH);
            if (current->euid() != 0 &&
                current->euid() != static_cast<u32>(fs::node_uid(resolved)))
                return cq_post_guarded(ring, s.user_data, -EPERM);
            return cq_post_guarded(
                ring, s.user_data,
                static_cast<i32>(fs::chmod_path(resolved, requested.value)));
        }

    } // namespace
#endif

    struct chmod : handler<chmod, opcode::chmod> {
        static bool invoke([[maybe_unused]] ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
#ifdef ASTIA_AIO_HOST_TEST
            return ::astia::io::aio::filesystem::dispatch(slot, ring, sqe);
#else
            return handle_chmod(ring, sqe);
#endif
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor chmod_descriptor = chmod::descriptor;

} // namespace astia::io::aio::operations

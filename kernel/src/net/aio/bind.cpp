#include "../../io/aio/handler.hpp"
#include "common.hpp"

namespace astia::io::aio::operations {

    struct bind : handler<bind, opcode::bind> {
        static bool invoke([[maybe_unused]] ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
            auto object = file_for(sqe.fd);
            if (!object)
                return cq_post_guarded(ring, sqe.user_data, -EBADF);
            const auto address = memory::user_ptr<astia_sockaddr>(sqe.address).validate();
            if (!address)
                return cq_post_guarded(ring, sqe.user_data, -EFAULT);
            astia_sockaddr a{};
            if (!memory::copy_in(&a, address.value.bytes()))
                return cq_post_guarded(ring, sqe.user_data, -EFAULT);
            char socket_path[fs::path_max]{}, canonical[fs::path_max]{};
            long path_rc = decode_unix_path(a, socket_path, sizeof socket_path);
            if (path_rc < 0)
                return cq_post_guarded(ring, sqe.user_data, static_cast<i32>(path_rc));
            path_rc = fs::resolve_path(socket_path, canonical, sizeof canonical, false);
            if (path_rc < 0)
                return cq_post_guarded(ring, sqe.user_data, static_cast<i32>(path_rc));
            if (!parent_writable(proc::current_process(), canonical))
                return cq_post_guarded(ring, sqe.user_data, -EACCES);
            const auto* socket = object->node()->unix_socket_capability();
            long        rc = socket == nullptr ? -EOPNOTSUPP : socket->bind(*object, a);
            return cq_post_guarded(ring, sqe.user_data, static_cast<i32>(rc));
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor bind_descriptor = bind::descriptor;

} // namespace astia::io::aio::operations

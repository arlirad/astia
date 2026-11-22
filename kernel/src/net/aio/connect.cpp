#include "../../io/aio/handler.hpp"
#ifdef ASTIA_AIO_HOST_TEST
#include "dispatch.hpp"
#else
#include "common.hpp"
#endif

namespace astia::io::aio::operations {

#ifndef ASTIA_AIO_HOST_TEST
    namespace {

        bool handle_connect(astia_rings* ring, const astia_sqe& s) {
            auto object = file_for(s.fd);
            if (!object)
                return cq_post_guarded(ring, s.user_data, -EBADF);
            const auto address = memory::user_ptr<astia_sockaddr>(s.address).validate();
            if (!address)
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            astia_sockaddr a{};
            if (!memory::copy_in(&a, address.value.bytes()))
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            char socket_path[fs::path_max]{}, canonical[fs::path_max]{};
            long path_rc = decode_unix_path(a, socket_path, sizeof socket_path);
            if (path_rc < 0)
                return cq_post_guarded(ring, s.user_data, static_cast<i32>(path_rc));
            path_rc = fs::resolve_path(socket_path, canonical, sizeof canonical, false);
            if (path_rc < 0)
                return cq_post_guarded(ring, s.user_data, static_cast<i32>(path_rc));
            if (!socket_node_writable(proc::current_process(), canonical))
                return cq_post_guarded(ring, s.user_data, -EACCES);
            const auto* socket = object->node()->unix_socket_capability();
            long rc = socket == nullptr ? -EOPNOTSUPP : socket->connect(*object, a);
            return cq_post_guarded(ring, s.user_data, static_cast<i32>(rc));
        }

    } // namespace
#endif

    struct connect : handler<connect, opcode::connect> {
        static bool invoke([[maybe_unused]] ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
#ifdef ASTIA_AIO_HOST_TEST
            return ::astia::io::aio::socket::dispatch(slot, ring, sqe);
#else
            return handle_connect(ring, sqe);
#endif
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor connect_descriptor = connect::descriptor;

} // namespace astia::io::aio::operations

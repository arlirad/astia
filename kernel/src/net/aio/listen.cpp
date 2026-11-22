#include "../../io/aio/handler.hpp"
#ifdef ASTIA_AIO_HOST_TEST
#include "dispatch.hpp"
#else
#include "common.hpp"
#endif

namespace astia::io::aio::operations {

#ifndef ASTIA_AIO_HOST_TEST
    namespace {

        bool handle_listen(astia_rings* ring, const astia_sqe& s) {
            auto object = file_for(s.fd);
            if (!object)
                return cq_post_guarded(ring, s.user_data, -EBADF);
            const auto* socket = object->node()->unix_socket_capability();
            return cq_post_guarded(
                ring, s.user_data,
                static_cast<i32>(socket == nullptr ? -EOPNOTSUPP
                                                   : socket->listen(*object, s.aux)));
        }

    } // namespace
#endif

    struct listen : handler<listen, opcode::listen> {
        static bool invoke([[maybe_unused]] ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
#ifdef ASTIA_AIO_HOST_TEST
            return ::astia::io::aio::socket::dispatch(slot, ring, sqe);
#else
            return handle_listen(ring, sqe);
#endif
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor listen_descriptor = listen::descriptor;

} // namespace astia::io::aio::operations

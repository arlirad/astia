#include "../../io/aio/handler.hpp"
#ifdef ASTIA_AIO_HOST_TEST
#include "dispatch.hpp"
#else
#include "common.hpp"
#endif

namespace astia::io::aio::operations {

#ifndef ASTIA_AIO_HOST_TEST
    namespace {

        bool handle_recvfrom(ring_slot& slot, astia_rings* ring, const astia_sqe& s) {
            mm::shptr<fs::open_file> object = file_for(s.fd);
            if (!object)
                return cq_post_guarded(ring, s.user_data, -EBADF);
            const auto address = memory::user_ptr<astia_sockaddr>(s.offset).validate();
            if (!memory::user_span_ok_for(span<u8>::bytes(s.address, s.length)) ||
                !address)
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            if (s.length > max_operation_bytes)
                return cq_post_guarded(ring, s.user_data, -EINVAL);
            if (s.length == 0)
                return cq_post_guarded(ring, s.user_data, 0);
            const auto* socket = object->node()->unix_socket_capability();
            long        rc = socket == nullptr ? -EOPNOTSUPP : socket->recvfrom(*object);
            if (rc < 0)
                return cq_post_guarded(ring, s.user_data, static_cast<i32>(rc));
            span<u8> range = span<u8>::bytes(s.address, s.length);
            return submit_socket_request(slot, ring, s, object, &range, 1,
                                         ::astia::io::direction::input);
        }

    } // namespace
#endif

    struct recvfrom : handler<recvfrom, opcode::recvfrom> {
        static bool invoke([[maybe_unused]] ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
#ifdef ASTIA_AIO_HOST_TEST
            return ::astia::io::aio::socket::dispatch(slot, ring, sqe);
#else
            return handle_recvfrom(slot, ring, sqe);
#endif
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor recvfrom_descriptor = recvfrom::descriptor;

} // namespace astia::io::aio::operations

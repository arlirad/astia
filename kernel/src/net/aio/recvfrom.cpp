#include "../../io/aio/handler.hpp"
#include "common.hpp"

namespace astia::io::aio::operations {

    struct recvfrom : handler<recvfrom, opcode::recvfrom> {
        static bool invoke([[maybe_unused]] ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
            mm::shptr<fs::open_file> object = file_for(sqe.fd);
            if (!object)
                return cq_post_guarded(ring, sqe.user_data, -EBADF);
            const auto address = memory::user_ptr<astia_sockaddr>(sqe.offset).validate();
            if (!memory::user_span_ok_for(span<u8>::bytes(sqe.address, sqe.length)) ||
                !address)
                return cq_post_guarded(ring, sqe.user_data, -EFAULT);
            if (sqe.length > max_operation_bytes)
                return cq_post_guarded(ring, sqe.user_data, -EINVAL);
            if (sqe.length == 0)
                return cq_post_guarded(ring, sqe.user_data, 0);
            const auto* socket = object->node()->unix_socket_capability();
            long        rc = socket == nullptr ? -EOPNOTSUPP : socket->recvfrom(*object);
            if (rc < 0)
                return cq_post_guarded(ring, sqe.user_data, static_cast<i32>(rc));
            span<u8> range = span<u8>::bytes(sqe.address, sqe.length);
            return submit_socket_request(slot, ring, sqe, object, &range, 1,
                                         ::astia::io::direction::input);
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor recvfrom_descriptor = recvfrom::descriptor;

} // namespace astia::io::aio::operations

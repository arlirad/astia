#include "../../io/aio/handler.hpp"
#include "common.hpp"

namespace astia::io::aio::operations {

    struct recvfrom : handler<recvfrom, opcode::recvfrom> {
        static core::task<aio_result>
        invoke([[maybe_unused]] ring::ring_slot& slot, [[maybe_unused]] astia_rings* ring,
               astia_sqe sqe, [[maybe_unused]] core::cltoken token) noexcept {
            mm::shptr<fs::open_file> object = file_for(sqe.fd);
            if (!object)
                co_return -EBADF;
            const auto address = memory::user_ptr<astia_sockaddr>(sqe.offset).validate();
            if (!memory::user_span_ok_for(span<u8>::bytes(sqe.address, sqe.length)) ||
                !address)
                co_return -EFAULT;
            if (sqe.length > max_operation_bytes)
                co_return -EINVAL;
            if (sqe.length == 0)
                co_return 0;
            const auto* socket = object->node()->unix_socket_capability();
            long        rc = socket == nullptr ? -EOPNOTSUPP : socket->recvfrom(*object);
            if (rc < 0)
                co_return static_cast<i32>(rc);
            span<u8> range = span<u8>::bytes(sqe.address, sqe.length);
            co_return submit_socket_request(slot, ring, sqe, object, &range, 1,
                                            ::astia::io::direction::input)
                ? aio_result::pending
                : aio_result::retry;
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor recvfrom_descriptor = recvfrom::descriptor;

} // namespace astia::io::aio::operations

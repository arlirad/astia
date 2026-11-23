#include "../../io/aio/handler.hpp"
#include "common.hpp"

namespace astia::io::aio::operations {

    struct sendto : handler<sendto, opcode::sendto> {
        static core::task<aio_result> invoke([[maybe_unused]] ring::ring_slot& slot,
                                             [[maybe_unused]] astia_rings*     ring,
                                             const astia_sqe& sqe) noexcept {
            mm::shptr<fs::open_file> object = file_for(sqe.fd);
            if (!object)
                co_return -EBADF;
            const auto address = memory::user_ptr<astia_sockaddr>(sqe.offset).validate();
            if (!memory::user_span_ok_for(span<u8>::bytes(sqe.address, sqe.length)) ||
                !address)
                co_return -EFAULT;
            if (sqe.length > max_operation_bytes)
                co_return -EINVAL;
            astia_sockaddr a{};
            if (!memory::copy_in(&a, address.value.bytes()))
                co_return -EFAULT;
            char socket_path[fs::path_max]{}, canonical[fs::path_max]{};
            long path_rc = decode_unix_path(a, socket_path, sizeof socket_path);
            if (path_rc < 0)
                co_return static_cast<i32>(path_rc);
            path_rc = fs::resolve_path(socket_path, canonical, sizeof canonical, false);
            if (path_rc < 0)
                co_return static_cast<i32>(path_rc);
            if (!socket_node_writable(proc::current_process(), canonical))
                co_return -EACCES;
            const auto* socket = object->node()->unix_socket_capability();
            long rc = socket == nullptr ? -EOPNOTSUPP : socket->sendto(*object, &a);
            if (rc < 0)
                co_return static_cast<i32>(rc);
            span<u8> range = span<u8>::bytes(sqe.address, sqe.length);
            co_return submit_socket_request(slot, ring, sqe, object, &range, 1,
                                            ::astia::io::direction::output)
                ? aio_result::pending
                : aio_result::retry;
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor sendto_descriptor = sendto::descriptor;

} // namespace astia::io::aio::operations

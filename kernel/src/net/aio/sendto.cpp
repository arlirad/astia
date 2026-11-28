#include "../../io/aio/handler.hpp"
#include "common.hpp"

namespace astia::io::aio::operations {
    struct sendto : handler<sendto, opcode::sendto> {
        static core::task<aio_result> invoke([[maybe_unused]] ring::ring_slot& slot,
                                             [[maybe_unused]] astia_rings*     ring,
                                             astia_sqe                         sqe,
                                             core::cltoken token) noexcept {
            mm::shptr<fs::open_file> handle = file_for(sqe.fd);
            if (!handle)
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

            char socket_path[fs::path_max]{};
            long path_rc = resolve_unix_path(a, socket_path, sizeof socket_path);
            if (path_rc < 0)
                co_return static_cast<i32>(path_rc);

            auto* current = proc::current_process();

            if (!socket_node_writable(current, socket_path))
                co_return -EACCES;

            const auto* socket = handle->parent()->unix_socket_capability();

            long rc =
                socket == nullptr ? -EOPNOTSUPP : socket->sendto(*handle, socket_path);
            if (rc < 0)
                co_return static_cast<i32>(rc);

            span<u8> range = span<u8>::bytes(sqe.address, sqe.length);

            co_return submit_socket_request(slot, ring, sqe, handle, &range, 1,
                                            ::astia::io::direction::output, false, false,
                                            token)
                ? aio_result::pending
                : aio_result::retry;
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor sendto_descriptor = sendto::descriptor;
} // namespace astia::io::aio::operations

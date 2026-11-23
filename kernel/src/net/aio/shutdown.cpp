#include "../../io/aio/handler.hpp"
#include "common.hpp"

namespace astia::io::aio::operations {

    struct shutdown : handler<shutdown, opcode::shutdown> {
        static core::task<aio_result> invoke([[maybe_unused]] ring::ring_slot& slot,
                                             [[maybe_unused]] astia_rings*     ring,
                                             const astia_sqe& sqe) noexcept {
            auto object = file_for(sqe.fd);
            if (!object)
                co_return -EBADF;
            const auto* socket = object->node()->unix_socket_capability();
            co_return static_cast<i32>(socket == nullptr
                                           ? -EOPNOTSUPP
                                           : socket->shutdown(*object, sqe.op_flags));
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor shutdown_descriptor = shutdown::descriptor;

} // namespace astia::io::aio::operations

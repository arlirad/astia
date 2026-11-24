#include "../../io/aio/handler.hpp"
#include "common.hpp"

namespace astia::io::aio::operations {

    struct listen : handler<listen, opcode::listen> {
        static core::task<aio_result>
        invoke([[maybe_unused]] ring::ring_slot& slot, [[maybe_unused]] astia_rings* ring,
               astia_sqe sqe, [[maybe_unused]] core::cltoken token) noexcept {
            auto handle = file_for(sqe.fd);
            if (!handle)
                co_return -EBADF;
            const auto* socket = handle->parent()->unix_socket_capability();
            co_return static_cast<i32>(
                socket == nullptr ? -EOPNOTSUPP : socket->listen(*handle, sqe.aux));
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor listen_descriptor = listen::descriptor;

} // namespace astia::io::aio::operations

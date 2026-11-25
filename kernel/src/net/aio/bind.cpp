#include "../../io/aio/handler.hpp"
#include "common.hpp"

namespace astia::io::aio::operations {

    struct bind : handler<bind, opcode::bind> {
        static core::task<aio_result>
        invoke([[maybe_unused]] ring::ring_slot& slot, [[maybe_unused]] astia_rings* ring,
               astia_sqe sqe, [[maybe_unused]] core::cltoken token) noexcept {
            auto handle = file_for(sqe.fd);
            if (!handle)
                co_return -EBADF;
            const auto address = memory::user_ptr<astia_sockaddr>(sqe.address).validate();
            if (!address)
                co_return -EFAULT;
            astia_sockaddr a{};
            if (!memory::copy_in(&a, address.value.bytes()))
                co_return -EFAULT;
            char socket_path[fs::path_max]{};
            long path_rc = decode_unix_path(a, socket_path, sizeof socket_path);
            if (path_rc < 0)
                co_return static_cast<i32>(path_rc);
            path_rc =
                fs::resolve_path(socket_path, socket_path, sizeof socket_path, false);
            if (path_rc < 0)
                co_return static_cast<i32>(path_rc);
            if (!parent_writable(proc::current_process(), socket_path))
                co_return -EACCES;
            const auto* socket = handle->parent()->unix_socket_capability();
            long        rc = socket == nullptr ? -EOPNOTSUPP : socket->bind(*handle, a);
            co_return static_cast<i32>(rc);
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor bind_descriptor = bind::descriptor;

} // namespace astia::io::aio::operations

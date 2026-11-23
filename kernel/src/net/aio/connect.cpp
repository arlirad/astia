#include "../../io/aio/handler.hpp"
#include "common.hpp"

namespace astia::io::aio::operations {

    struct connect : handler<connect, opcode::connect> {
        static core::task<aio_result>
        invoke([[maybe_unused]] ring::ring_slot& slot, [[maybe_unused]] astia_rings* ring,
               astia_sqe sqe, [[maybe_unused]] core::cltoken token) noexcept {
            auto object = file_for(sqe.fd);
            if (!object)
                co_return -EBADF;
            const auto address = memory::user_ptr<astia_sockaddr>(sqe.address).validate();
            if (!address)
                co_return -EFAULT;
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
            long rc = socket == nullptr ? -EOPNOTSUPP : socket->connect(*object, a);
            co_return static_cast<i32>(rc);
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor connect_descriptor = connect::descriptor;

} // namespace astia::io::aio::operations

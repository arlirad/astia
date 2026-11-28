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

            // AF_UNIX bindings live in the filesystem namespace; the family
            // implementation resolves and registers the path.  Other
            // families resolve their own addresses.
            if (a.family == AF_UNIX) {
                char socket_path[fs::path_max]{};
                long path_rc = decode_unix_path(a, socket_path, sizeof socket_path);
                if (path_rc < 0)
                    co_return static_cast<i32>(path_rc);

                auto* current = proc::current_process();
                if (current == nullptr)
                    co_return -ESRCH;

                path_rc = current->resolve_path(socket_path, socket_path,
                                                sizeof socket_path, false);
                if (path_rc < 0)
                    co_return static_cast<i32>(path_rc);

                if (!parent_writable(current, socket_path))
                    co_return -EACCES;
            }

            if (auto* sock = dgram_for(handle))
                co_return static_cast<i32>(sock->bind(a));

            if (auto* sock = stream_for(handle))
                co_return static_cast<i32>(sock->bind(a));

            co_return -EOPNOTSUPP;
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor bind_descriptor = bind::descriptor;
} // namespace astia::io::aio::operations

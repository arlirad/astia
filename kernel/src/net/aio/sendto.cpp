#include <astia/mm/heap.hpp>

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

            if (a.family == AF_UNIX) {
                char socket_path[fs::path_max]{};
                long path_rc = resolve_unix_path(a, socket_path, sizeof socket_path);
                if (path_rc < 0)
                    co_return static_cast<i32>(path_rc);

                auto* current = proc::current_process();
                if (current == nullptr)
                    co_return -ESRCH;

                if (!socket_node_writable(current, socket_path))
                    co_return -EACCES;
            }

            auto* socket = dgram_for(handle);
            if (socket == nullptr)
                co_return -EOPNOTSUPP;

            auto storage = mm::heap::alloc(sqe.length == 0 ? 1 : sqe.length);
            if (!storage)
                co_return -ENOMEM;

            span<u8> kernel(storage.value, sqe.length);

            if (sqe.length != 0 &&
                !memory::copy_in(kernel.data(),
                                 span<u8>::bytes(sqe.address, sqe.length))) {
                mm::heap::free(storage.value);

                co_return -EFAULT;
            }

            const auto result = co_await socket->sendto(
                kernel, a, token,
                (handle->flags() &
                 static_cast<u32>(fs::open_file_status_flags::nonblock)) != 0);

            mm::heap::free(storage.value);

            if (!result)
                co_return static_cast<i32>(error_errno(result.error));

            co_return static_cast<i32>(result.value);
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor sendto_descriptor = sendto::descriptor;
} // namespace astia::io::aio::operations

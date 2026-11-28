#include "../../io/aio/handler.hpp"
#include "common.hpp"

namespace astia::io::aio::operations {
    struct accept : handler<accept, opcode::accept> {
        static core::task<aio_result> invoke([[maybe_unused]] ring::ring_slot& slot,
                                             [[maybe_unused]] astia_rings*     ring,
                                             astia_sqe                         sqe,
                                             core::cltoken token) noexcept {
            auto handle = file_for(sqe.fd);
            if (!handle)
                co_return -EBADF;

            if (stream_for(handle) == nullptr)
                co_return -EOPNOTSUPP;

            if (sqe.address != 0 &&
                !memory::user_ptr<astia_sockaddr>(sqe.address).validate())
                co_return -EFAULT;

            co_return dispatch::submit_socket_request(slot, ring, sqe, handle, nullptr, 0,
                                                      ::astia::io::direction::input, true,
                                                      true, token)
                ? aio_result::pending
                : aio_result::retry;
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor accept_descriptor = accept::descriptor;
} // namespace astia::io::aio::operations

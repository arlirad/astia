#include "../../io/aio/handler.hpp"
#include "common.hpp"

namespace astia::io::aio::operations {

    struct accept : handler<accept, opcode::accept> {
        static core::task<aio_result> invoke([[maybe_unused]] ring::ring_slot& slot,
                                             [[maybe_unused]] astia_rings*     ring,
                                             const astia_sqe& sqe) noexcept {
            auto object = file_for(sqe.fd);
            if (!object)
                co_return -EBADF;
            if (object->node()->unix_socket_capability() == nullptr)
                co_return -EOPNOTSUPP;
            co_return dispatch::submit_socket_request(slot, ring, sqe, object, nullptr, 0,
                                                      ::astia::io::direction::input, true,
                                                      true)
                ? aio_result::pending
                : aio_result::retry;
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor accept_descriptor = accept::descriptor;

} // namespace astia::io::aio::operations

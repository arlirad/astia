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

            auto* stream = stream_for(handle);
            if (stream == nullptr)
                co_return -EOPNOTSUPP;

            const auto address = memory::user_ptr<astia_sockaddr>(sqe.address).validate();

            if (sqe.address != 0 && !address)
                co_return -EFAULT;

            const auto result = co_await stream->accept(token);
            if (!result)
                co_return static_cast<i32>(error_errno(result.error));

            const fs::accepted accepted = result.value;

            if (accepted.fd < 0)
                co_return static_cast<i32>(accepted.fd);

            if (address && !memory::copy_out_for(proc::current_process(),
                                                 address.value.bytes(), &accepted.peer))
                co_return -EFAULT;

            co_return static_cast<i32>(accepted.fd);
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor accept_descriptor = accept::descriptor;
} // namespace astia::io::aio::operations

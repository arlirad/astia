#include "../../io/aio/handler.hpp"
#include "common.hpp"

namespace astia::io::aio::operations {

    struct fsync : handler<fsync, opcode::fsync> {
        static core::task<aio_result>
        invoke([[maybe_unused]] ring::ring_slot& slot, [[maybe_unused]] astia_rings* ring,
               astia_sqe sqe, [[maybe_unused]] core::cltoken token) noexcept {
            auto handle = file_for(sqe.fd);
            if (!handle)
                co_return -EBADF;
            if (!handle->mode().is_regular())
                co_return -EINVAL;
            co_return static_cast<i32>(handle->sync());
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor fsync_descriptor = fsync::descriptor;

} // namespace astia::io::aio::operations

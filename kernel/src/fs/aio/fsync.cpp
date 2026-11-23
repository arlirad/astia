#include "../../io/aio/handler.hpp"
#include "common.hpp"

namespace astia::io::aio::operations {

    struct fsync : handler<fsync, opcode::fsync> {
        static core::task<aio_result> invoke([[maybe_unused]] ring::ring_slot& slot,
                                             [[maybe_unused]] astia_rings*     ring,
                                             astia_sqe sqe) noexcept {
            auto object = file_for(sqe.fd);
            if (!object)
                co_return -EBADF;
            if (!object->mode().is_regular())
                co_return -EINVAL;
            co_return static_cast<i32>(object->sync());
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor fsync_descriptor = fsync::descriptor;

} // namespace astia::io::aio::operations

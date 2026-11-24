#include "../../io/aio/handler.hpp"
#include "common.hpp"

namespace astia::io::aio::operations {

    struct ftruncate : handler<ftruncate, opcode::ftruncate> {
        static core::task<aio_result>
        invoke([[maybe_unused]] ring::ring_slot& slot, [[maybe_unused]] astia_rings* ring,
               astia_sqe sqe, [[maybe_unused]] core::cltoken token) noexcept {
            auto handle = file_for(sqe.fd);
            if (!handle)
                co_return -EBADF;
            if ((handle->flags() & fs::open_file_access_mask) == 0)
                co_return -EBADF;
            co_return static_cast<i32>(handle->truncate(sqe.offset));
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor ftruncate_descriptor = ftruncate::descriptor;

} // namespace astia::io::aio::operations

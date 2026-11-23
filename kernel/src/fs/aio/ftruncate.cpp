#include "../../io/aio/handler.hpp"
#include "common.hpp"

namespace astia::io::aio::operations {

    struct ftruncate : handler<ftruncate, opcode::ftruncate> {
        static core::task<aio_result> invoke([[maybe_unused]] ring::ring_slot& slot,
                                             [[maybe_unused]] astia_rings*     ring,
                                             const astia_sqe& sqe) noexcept {
            auto object = file_for(sqe.fd);
            if (!object)
                co_return -EBADF;
            if ((object->flags() & fs::open_file_access_mask) == 0)
                co_return -EBADF;
            co_return static_cast<i32>(object->truncate(sqe.offset));
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor ftruncate_descriptor = ftruncate::descriptor;

} // namespace astia::io::aio::operations

#include "common.hpp"

namespace astia::io::aio::operations {
    struct write : handler<write, opcode::write> {
        static core::task<aio_result> invoke(ring::ring_slot& slot, astia_rings* ring,
                                             astia_sqe     sqe,
                                             core::cltoken token) noexcept {
            auto io = dispatch::prepare_file_io(slot, ring, sqe,
                                                ::astia::io::direction::output, false);
            if (!io)
                co_return io.error;

            if (io.value.bytes == 0)
                co_return 0;

            auto result =
                co_await io.value.handle->writev(io.value.view(), sqe.offset, token);
            if (!result)
                co_return result.error;

            co_return static_cast<i64>(result.value);
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor write_descriptor = write::descriptor;
} // namespace astia::io::aio::operations

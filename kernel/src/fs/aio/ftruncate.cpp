#include "../../io/aio/handler.hpp"
#include "dispatch.hpp"

namespace astia::io::aio::operations {

    struct ftruncate : handler<ftruncate, opcode::ftruncate> {
        static bool invoke(ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
            return ::astia::io::aio::filesystem::dispatch(slot, ring, sqe);
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor ftruncate_descriptor = ftruncate::descriptor;

} // namespace astia::io::aio::operations

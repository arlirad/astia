#include "../../io/aio/handler.hpp"
#include "common.hpp"

namespace astia::io::aio::operations {

    struct close : handler<close, opcode::close> {
        static core::task<aio_result> invoke([[maybe_unused]] ring::ring_slot& slot,
                                             [[maybe_unused]] astia_rings*     ring,
                                             astia_sqe sqe) noexcept {
            proc::process* current = proc::current_process();
            auto           mapped  = mapper<fd_t>::from(sqe.fd);
            if (current == nullptr || !mapped.has_value ||
                !current->descriptor(mapped.value)) {
                co_return -EBADF;
            }
            current->clear_descriptor(mapped.value);
            co_return 0;
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor close_descriptor = close::descriptor;

} // namespace astia::io::aio::operations

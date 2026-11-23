#include "common.hpp"

namespace astia::io::aio::operations {

    struct dup : handler<dup, opcode::dup> {
        static core::task<aio_result> invoke([[maybe_unused]] ring::ring_slot& slot,
                                             [[maybe_unused]] astia_rings*     ring,
                                             const astia_sqe& sqe) noexcept {
            proc::process*           current = proc::current_process();
            mm::shptr<fs::open_file> source  = file_for(sqe.fd);
            if (current == nullptr || !source) {
                co_return -EBADF;
            }
            const auto fd =
                current->duplicate_descriptor(mapper<fd_t>::from(sqe.fd).value);
            co_return fd ? static_cast<i64>(fd.value.value) : -EMFILE;
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor dup_descriptor = dup::descriptor;

} // namespace astia::io::aio::operations

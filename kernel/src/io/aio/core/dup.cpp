#include "common.hpp"

namespace astia::io::aio::operations {

    namespace {

        bool handle_dup(astia_rings* ring, const astia_sqe& s) {
            proc::process*           current = proc::current_process();
            mm::shptr<fs::open_file> source  = file_for(s.fd);
            if (current == nullptr || !source) {
                return cq_post_guarded(ring, s.user_data, -EBADF);
            }
            const auto fd = current->duplicate_descriptor(mapper<fd_t>::from(s.fd).value);
            return cq_post_guarded(ring, s.user_data,
                                   fd ? static_cast<i64>(fd.value.value) : -EMFILE);
        }

    } // namespace

    struct dup : handler<dup, opcode::dup> {
        static bool invoke([[maybe_unused]] ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
            return handle_dup(ring, sqe);
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor dup_descriptor = dup::descriptor;

} // namespace astia::io::aio::operations

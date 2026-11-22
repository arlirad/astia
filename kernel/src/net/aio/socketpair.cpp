#include "../../io/aio/handler.hpp"
#include "common.hpp"

namespace astia::io::aio::operations {

    namespace {

        bool handle_socketpair(astia_rings* ring, const astia_sqe& s) {
            if (s.fd != AF_UNIX)
                return cq_post_guarded(ring, s.user_data, -EAFNOSUPPORT);
            if (s.aux != 0)
                return cq_post_guarded(ring, s.user_data, -EPROTONOSUPPORT);
            if (s.op_flags != SOCK_STREAM && s.op_flags != SOCK_DGRAM)
                return cq_post_guarded(ring, s.user_data, -EPROTOTYPE);
            if (s.length != 2 * sizeof(i32))
                return cq_post_guarded(ring, s.user_data, -EINVAL);
            if (!memory::user_span_writable_ok_for(proc::current_process(),
                                                   span<u8>::bytes(s.address, s.length)))
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            proc::process* current = proc::current_process();
            if (!current)
                return cq_post_guarded(ring, s.user_data, -ESRCH);
            mm::shptr<fs::open_file> a, b;
            long                     rc = fs::unix_socketpair(a, b, s.op_flags);
            if (rc < 0)
                return cq_post_guarded(ring, s.user_data, (i32) rc);
            i32 fds[2] = {-1, -1};
            for (i32 fd = 3; fd < (i32) proc::max_process_fds && fds[1] < 0; ++fd) {
                if (fds[0] < 0 && !current->descriptor(fd_t{fd}) &&
                    current->set_descriptor(fd_t{fd}, a))
                    fds[0] = fd;
                else if (fds[0] >= 0 && !current->descriptor(fd_t{fd}) &&
                         current->set_descriptor(fd_t{fd}, b))
                    fds[1] = fd;
            }
            if (fds[1] < 0) {
                if (fds[0] >= 0)
                    current->clear_descriptor(fd_t{fds[0]});
                return cq_post_guarded(ring, s.user_data, -EMFILE);
            }
            if (!memory::copy_out(span<u8>::bytes(s.address, s.length), fds)) {
                current->clear_descriptor(fd_t{fds[0]});
                current->clear_descriptor(fd_t{fds[1]});
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            }
            return cq_post_guarded(ring, s.user_data, 0);
        }

    } // namespace

    struct socketpair : handler<socketpair, opcode::socketpair> {
        static bool invoke([[maybe_unused]] ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
            return handle_socketpair(ring, sqe);
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor socketpair_descriptor = socketpair::descriptor;

} // namespace astia::io::aio::operations

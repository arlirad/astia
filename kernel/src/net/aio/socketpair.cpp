#include "../../io/aio/handler.hpp"
#include "common.hpp"

namespace astia::io::aio::operations {
    struct socketpair : handler<socketpair, opcode::socketpair> {
        static core::task<aio_result>
        invoke([[maybe_unused]] ring::ring_slot& slot, [[maybe_unused]] astia_rings* ring,
               astia_sqe sqe, [[maybe_unused]] core::cltoken token) noexcept {
            if (sqe.fd != AF_UNIX)
                co_return -EAFNOSUPPORT;

            if (sqe.aux != 0)
                co_return -EPROTONOSUPPORT;

            if (sqe.op_flags != SOCK_STREAM && sqe.op_flags != SOCK_DGRAM)
                co_return -EPROTOTYPE;

            if (sqe.length != 2 * sizeof(i32))
                co_return -EINVAL;

            if (!memory::user_span_writable_ok_for(
                    proc::current_process(), span<u8>::bytes(sqe.address, sqe.length)))
                co_return -EFAULT;

            proc::process* current = proc::current_process();
            if (!current)
                co_return -ESRCH;

            mm::shptr<fs::open_file> a, b;

            long rc = fs::unix_socketpair(a, b, sqe.op_flags);
            if (rc < 0)
                co_return (i32) rc;

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

                co_return -EMFILE;
            }

            if (!memory::copy_out(span<u8>::bytes(sqe.address, sqe.length), fds)) {
                current->clear_descriptor(fd_t{fds[0]});
                current->clear_descriptor(fd_t{fds[1]});

                co_return -EFAULT;
            }

            co_return 0;
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor socketpair_descriptor = socketpair::descriptor;
} // namespace astia::io::aio::operations

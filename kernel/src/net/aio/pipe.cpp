#include "../../io/aio/handler.hpp"
#include "common.hpp"

namespace astia::io::aio::operations {

    struct pipe : handler<pipe, opcode::pipe> {
        static bool invoke([[maybe_unused]] ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
            const auto out = memory::user_ptr<i32>(sqe.address).validate(2);
            if (!out)
                return cq_post_guarded(ring, sqe.user_data, -EFAULT);
            proc::process* current = proc::current_process();
            if (current == nullptr)
                return cq_post_guarded(ring, sqe.user_data, -ESRCH);
            auto pipe = fs::create_pipe();
            if (!pipe)
                return cq_post_guarded(ring, sqe.user_data,
                                       static_cast<i32>(-astia::errno_value(pipe.error)));
            auto& read_end  = pipe.value.first;
            auto& write_end = pipe.value.second;
            i32   read_fd   = -1;
            i32   write_fd  = -1;
            for (i32 fd = 3; fd < static_cast<i32>(proc::max_process_fds); ++fd) {
                if (read_fd < 0 && !current->descriptor(fd_t{fd})) {
                    if (current->set_descriptor(fd_t{fd}, read_end))
                        read_fd = fd;
                }
                else if (write_fd < 0 && !current->descriptor(fd_t{fd})) {
                    if (current->set_descriptor(fd_t{fd}, write_end)) {
                        write_fd = fd;
                        break;
                    }
                }
            }
            if (read_fd < 0 || write_fd < 0) {
                if (read_fd >= 0)
                    current->clear_descriptor(fd_t{read_fd});
                return cq_post_guarded(ring, sqe.user_data, -EMFILE);
            }
            i32 result[2] = {read_fd, write_fd};
            if (!memory::copy_out(out.value.bytes(2), result)) {
                current->clear_descriptor(fd_t{read_fd});
                current->clear_descriptor(fd_t{write_fd});
                return cq_post_guarded(ring, sqe.user_data, -EFAULT);
            }
            return cq_post_guarded(ring, sqe.user_data, 0);
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor pipe_descriptor = pipe::descriptor;

} // namespace astia::io::aio::operations

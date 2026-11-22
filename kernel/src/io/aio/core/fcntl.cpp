#include "common.hpp"

namespace astia::io::aio::operations {

    struct fcntl : handler<fcntl, opcode::fcntl> {
        static bool invoke([[maybe_unused]] ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
            proc::process* current = proc::current_process();
            auto           mapped  = mapper<fd_t>::from(sqe.fd);
            if (current == nullptr || !mapped.has_value ||
                !current->descriptor(mapped.value))
                return cq_post_guarded(ring, sqe.user_data, -EBADF);
            const u32 argument = sqe.aux;
            switch (sqe.op_flags) {
            case ASTIA_F_DUPFD:
            case ASTIA_F_DUPFD_CLOEXEC: {
                if (static_cast<i32>(argument) < 0)
                    return cq_post_guarded(ring, sqe.user_data, -EINVAL);
                const u32 flags =
                    sqe.op_flags == ASTIA_F_DUPFD_CLOEXEC ? ASTIA_FD_CLOEXEC : 0;
                const auto result = current->duplicate_descriptor(
                    mapped.value, fd_t{static_cast<i32>(argument)}, flags);
                return cq_post_guarded(ring, sqe.user_data,
                                       result ? result.value.value : -EMFILE);
            }
            case ASTIA_F_GETFD:
                return cq_post_guarded(
                    ring, sqe.user_data,
                    static_cast<i32>(current->descriptor_flags(mapped.value)));
            case ASTIA_F_SETFD:
                if ((argument & ~ASTIA_FD_CLOEXEC) != 0)
                    return cq_post_guarded(ring, sqe.user_data, -EINVAL);
                if (!current->set_descriptor_flags(mapped.value, argument))
                    return cq_post_guarded(ring, sqe.user_data, -EBADF);
                return cq_post_guarded(ring, sqe.user_data, 0);
            case ASTIA_F_GETFL: {
                const u32 flags = current->descriptor(mapped.value)->flags();
                u32       result;
                if ((flags & fs::pipe_write_endpoint_flag) != 0)
                    result = ASTIA_AIO_O_WRONLY;
                else if ((flags & fs::pipe_read_endpoint_flag) != 0)
                    result = ASTIA_AIO_O_RDONLY;
                else
                    result = (flags & ASTIA_AIO_O_ACCMODE) == ASTIA_AIO_O_RDWR
                                 ? ASTIA_AIO_O_RDWR
                                 : ((flags & ASTIA_AIO_O_WRONLY) ? ASTIA_AIO_O_WRONLY
                                                                 : ASTIA_AIO_O_RDONLY);
                if (flags & static_cast<u32>(fs::open_file_status_flags::append))
                    result |= ASTIA_O_APPEND;
                if (flags & static_cast<u32>(fs::open_file_status_flags::nonblock))
                    result |= ASTIA_O_NONBLOCK;
                return cq_post_guarded(ring, sqe.user_data, static_cast<i32>(result));
            }
            case ASTIA_F_SETFL: {
                auto object = current->descriptor(mapped.value);
                object->set_status_flags(
                    (argument & ASTIA_O_APPEND
                         ? static_cast<u32>(fs::open_file_status_flags::append)
                         : 0u) |
                    (argument & ASTIA_O_NONBLOCK
                         ? static_cast<u32>(fs::open_file_status_flags::nonblock)
                         : 0u));
                return cq_post_guarded(ring, sqe.user_data, 0);
            }
            default:
                return cq_post_guarded(ring, sqe.user_data, -EINVAL);
            }
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor fcntl_descriptor = fcntl::descriptor;

} // namespace astia::io::aio::operations

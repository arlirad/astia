#include "../../io/aio/handler.hpp"
#include "common.hpp"

namespace astia::io::aio::operations {
    struct recvmsg : handler<recvmsg, opcode::recvmsg> {
        static core::task<aio_result> invoke([[maybe_unused]] ring::ring_slot& slot,
                                             [[maybe_unused]] astia_rings*     ring,
                                             astia_sqe                         sqe,
                                             core::cltoken token) noexcept {
            mm::shptr<fs::open_file> handle = file_for(sqe.fd);
            if (!handle)
                co_return -EBADF;

            if (sqe.length > max_operation_bytes)
                co_return -EINVAL;

            proc::process* current = proc::current_process();
            if (!current)
                co_return -ESRCH;

            span<u8> data = span<u8>::bytes(sqe.address, sqe.length);

            if (!memory::user_span_writable_ok_for(current, data))
                co_return -EFAULT;

            const auto header_ptr = memory::user_ptr<astia_msghdr>(sqe.offset).validate();
            if (!header_ptr)
                co_return -EFAULT;

            astia_msghdr header{};

            if (!memory::copy_in(&header, header_ptr.value.bytes()))
                co_return -EFAULT;

            if (header.controllen != 0) {
                u32 capacity = header.controllen;

                if (capacity > fs::unix_control_max_bytes)
                    capacity = fs::unix_control_max_bytes;

                if (header.control == nullptr ||
                    !memory::user_span_writable_ok_for(
                        current, span<u8>(static_cast<u8*>(header.control), capacity)))
                    co_return -EFAULT;
            }

            if (sqe.length == 0 && header.controllen != 0)
                co_return -EOPNOTSUPP;

            if ((header.flags & ASTIA_MSG_DONTWAIT) != 0 && handle->poll(POLLIN) == 0)
                co_return -EAGAIN;

            auto* dgram  = dgram_for(handle);
            auto* stream = stream_for(handle);

            if (dgram == nullptr && stream == nullptr)
                co_return -EOPNOTSUPP;

            const long prepared =
                dgram != nullptr ? dgram->recvmsg_prepare() : stream->recvmsg_prepare();
            if (prepared < 0)
                co_return static_cast<i32>(prepared);

            co_return submit_socket_request(slot, ring, sqe, handle, &data, 1,
                                            ::astia::io::direction::input, true, false,
                                            token)
                ? aio_result::pending
                : aio_result::retry;
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor recvmsg_descriptor = recvmsg::descriptor;
} // namespace astia::io::aio::operations

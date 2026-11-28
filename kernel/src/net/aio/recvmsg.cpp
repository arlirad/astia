#include <astia/mm/heap.hpp>

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

            auto* current = proc::current_process();
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

            auto* dgram  = dgram_for(handle);
            auto* stream = stream_for(handle);

            if (dgram == nullptr && stream == nullptr)
                co_return -EOPNOTSUPP;

            if (sqe.length == 0)
                co_return header.controllen != 0 ? -EOPNOTSUPP : 0;

            auto storage = mm::heap::alloc(sqe.length);
            if (!storage)
                co_return -ENOMEM;

            span<u8> kernel(storage.value, sqe.length);

            optional<fs::message> received;

            if (dgram)
                received = co_await dgram->recvmsg(kernel, token);
            else
                received = co_await stream->recvmsg(kernel, token);

            if (!received) {
                mm::heap::free(storage.value);

                co_return static_cast<i32>(error_errno(received.error));
            }

            fs::message message = received.value;

            if (message.length < 0) {
                mm::heap::free(storage.value);

                co_return static_cast<i32>(message.length);
            }

            const usz moved = message.length < static_cast<i64>(kernel.size())
                                  ? static_cast<usz>(message.length)
                                  : kernel.size();

            if (!memory::copy_out_for(current, span<u8>(data.data(), moved),
                                      storage.value)) {
                mm::heap::free(storage.value);

                co_return -EFAULT;
            }

            mm::heap::free(storage.value);

            u8  control[fs::unix_control_max_bytes]{};
            u32 control_used  = 0;
            u32 message_flags = 0;

            if (message.control && header.controllen != 0) {
                u32 capacity = header.controllen;

                if (capacity > fs::unix_control_max_bytes)
                    capacity = fs::unix_control_max_bytes;

                const long built = fs::build_unix_control(
                    *current, *message.control, header.flags, span<u8>(control, capacity),
                    control_used, message_flags);
                if (built < 0)
                    co_return static_cast<i32>(built);

                if (control_used != 0 &&
                    !memory::copy_out_for(
                        current, span<u8>(static_cast<u8*>(header.control), control_used),
                        control))
                    co_return -EFAULT;
            }

            header.controllen = control_used;
            header.msg_flags  = message_flags;

            if (!memory::copy_out_for(current, header_ptr.value.bytes(), &header))
                co_return -EFAULT;

            co_return static_cast<i32>(message.length);
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor recvmsg_descriptor = recvmsg::descriptor;
} // namespace astia::io::aio::operations

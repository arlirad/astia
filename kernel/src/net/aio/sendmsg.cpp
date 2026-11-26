#include "../../io/aio/handler.hpp"
#include "common.hpp"

namespace astia::io::aio::operations {
    struct sendmsg : handler<sendmsg, opcode::sendmsg> {
        static core::task<aio_result> invoke([[maybe_unused]] ring::ring_slot& slot,
                                             [[maybe_unused]] astia_rings*     ring,
                                             astia_sqe                         sqe,
                                             core::cltoken token) noexcept {
            mm::shptr<fs::open_file> handle = file_for(sqe.fd);
            if (!handle)
                co_return -EBADF;

            if (sqe.length > max_operation_bytes)
                co_return -EINVAL;

            span<u8> data = span<u8>::bytes(sqe.address, sqe.length);

            if (!memory::user_span_ok_for(data))
                co_return -EFAULT;

            const auto header_ptr = memory::user_ptr<astia_msghdr>(sqe.offset).validate();
            if (!header_ptr)
                co_return -EFAULT;

            astia_msghdr header{};

            if (!memory::copy_in(&header, header_ptr.value.bytes()))
                co_return -EFAULT;

            const auto* socket = handle->parent()->unix_socket_capability();

            if (socket == nullptr || socket->set_send_control == nullptr ||
                socket->sendmsg == nullptr)
                co_return -EOPNOTSUPP;

            mm::shptr<fs::unix_control> control{};

            if (header.controllen != 0) {
                if (header.control == nullptr ||
                    header.controllen > fs::unix_control_max_bytes)
                    co_return -EINVAL;

                span<u8> user_control =
                    span<u8>(static_cast<u8*>(header.control), header.controllen);

                if (!memory::user_span_ok_for(user_control))
                    co_return -EFAULT;

                u8 control_bytes[fs::unix_control_max_bytes]{};

                if (!memory::copy_in(control_bytes, user_control))
                    co_return -EFAULT;

                proc::process* current = proc::current_process();
                if (!current)
                    co_return -ESRCH;

                const long parsed = fs::parse_unix_control(
                    *current, span<const u8>(control_bytes, header.controllen), control);
                if (parsed < 0)
                    co_return static_cast<i32>(parsed);
            }

            const long prepared = socket->set_send_control(*handle, control);
            if (prepared < 0)
                co_return static_cast<i32>(prepared);

            if (sqe.length == 0)
                co_return control ? -EOPNOTSUPP : 0;

            co_return submit_socket_request(slot, ring, sqe, handle, &data, 1,
                                            ::astia::io::direction::output, true, false,
                                            token)
                ? aio_result::pending
                : aio_result::retry;
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor sendmsg_descriptor = sendmsg::descriptor;
} // namespace astia::io::aio::operations

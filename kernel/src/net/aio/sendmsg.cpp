#include <astia/mm/heap.hpp>

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

            auto* dgram  = dgram_for(handle);
            auto* stream = stream_for(handle);

            if (dgram == nullptr && stream == nullptr)
                co_return -EOPNOTSUPP;

            const u64 destination = msghdr_destination(header);

            astia_sockaddr destination_address{};

            if (destination != 0) {
                const auto name =
                    memory::user_ptr<astia_sockaddr>(destination).validate();
                if (!name)
                    co_return -EFAULT;

                if (!memory::copy_in(&destination_address, name.value.bytes()))
                    co_return -EFAULT;

                if (destination_address.family == AF_UNIX) {
                    char socket_path[fs::path_max]{};
                    long path_rc = resolve_unix_path(destination_address, socket_path,
                                                     sizeof socket_path);
                    if (path_rc < 0)
                        co_return static_cast<i32>(path_rc);

                    auto* current = proc::current_process();
                    if (current == nullptr)
                        co_return -ESRCH;

                    if (!socket_node_writable(current, socket_path))
                        co_return -EACCES;
                }
            }

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

                auto* current = proc::current_process();
                if (!current)
                    co_return -ESRCH;

                const long parsed = fs::parse_unix_control(
                    *current, span<const u8>(control_bytes, header.controllen), control);
                if (parsed < 0)
                    co_return static_cast<i32>(parsed);
            }

            if (sqe.length == 0)
                co_return control ? -EOPNOTSUPP : 0;

            auto storage = mm::heap::alloc(sqe.length);
            if (!storage)
                co_return -ENOMEM;

            span<u8> kernel(storage.value, sqe.length);

            if (!memory::copy_in(kernel.data(), data)) {
                mm::heap::free(storage.value);

                co_return -EFAULT;
            }

            optional<i64> result;

            if (dgram)
                result = co_await dgram->sendmsg(
                    kernel, destination != 0 ? &destination_address : nullptr, control,
                    token);
            else
                result = co_await stream->sendmsg(kernel, control, token);

            mm::heap::free(storage.value);

            if (!result)
                co_return static_cast<i32>(error_errno(result.error));

            co_return static_cast<i32>(result.value);
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor sendmsg_descriptor = sendmsg::descriptor;
} // namespace astia::io::aio::operations

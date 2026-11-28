#include <astia/mm/heap.hpp>

#include "../../io/aio/handler.hpp"
#include "common.hpp"

namespace astia::io::aio::operations {
    struct recvfrom : handler<recvfrom, opcode::recvfrom> {
        static core::task<aio_result> invoke([[maybe_unused]] ring::ring_slot& slot,
                                             [[maybe_unused]] astia_rings*     ring,
                                             astia_sqe                         sqe,
                                             core::cltoken token) noexcept {
            mm::shptr<fs::open_file> handle = file_for(sqe.fd);
            if (!handle)
                co_return -EBADF;

            const auto address = memory::user_ptr<astia_sockaddr>(sqe.offset).validate();

            auto* current = proc::current_process();
            if (!current)
                co_return -ESRCH;

            if (!memory::user_span_writable_ok_for(
                    current, span<u8>::bytes(sqe.address, sqe.length)) ||
                !address ||
                !memory::user_span_writable_ok_for(current, address.value.bytes()))
                co_return -EFAULT;

            if (sqe.length > max_operation_bytes)
                co_return -EINVAL;

            if (sqe.length == 0)
                co_return 0;

            auto* socket = dgram_for(handle);
            if (socket == nullptr)
                co_return -EOPNOTSUPP;

            auto storage = mm::heap::alloc(sqe.length);
            if (!storage)
                co_return -ENOMEM;

            span<u8> kernel(storage.value, sqe.length);

            const auto received = co_await socket->recvfrom(kernel, token);

            if (!received) {
                mm::heap::free(storage.value);

                co_return static_cast<i32>(error_errno(received.error));
            }

            const fs::message message = received.value;

            if (message.length < 0) {
                mm::heap::free(storage.value);

                co_return static_cast<i32>(message.length);
            }

            const usz moved = message.length < static_cast<i64>(kernel.size())
                                  ? static_cast<usz>(message.length)
                                  : kernel.size();

            if (!memory::copy_out_for(current, span<u8>::bytes(sqe.address, moved),
                                      storage.value)) {
                mm::heap::free(storage.value);

                co_return -EFAULT;
            }

            mm::heap::free(storage.value);

            if (!memory::copy_out_for(current, address.value.bytes(), &message.source))
                co_return -EFAULT;

            co_return static_cast<i32>(message.length);
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor recvfrom_descriptor = recvfrom::descriptor;
} // namespace astia::io::aio::operations

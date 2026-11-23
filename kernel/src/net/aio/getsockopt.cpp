#include "../../io/aio/handler.hpp"
#include "common.hpp"

namespace astia::io::aio::operations {

    struct getsockopt : handler<getsockopt, opcode::getsockopt> {
        static core::task<aio_result>
        invoke([[maybe_unused]] ring::ring_slot& slot, [[maybe_unused]] astia_rings* ring,
               astia_sqe sqe, [[maybe_unused]] core::cltoken token) noexcept {
            auto object = file_for(sqe.fd);
            if (!object)
                co_return -EBADF;
            auto length = memory::user_ptr<u32>(sqe.offset).validate();
            if (!length || !memory::user_span_writable_ok_for(proc::current_process(),
                                                              length.value.bytes()))
                co_return -EFAULT;
            u32 capacity = 0;
            if (!memory::copy_in(&capacity, length.value.bytes()))
                co_return -EFAULT;
            if (capacity > max_operation_bytes)
                co_return -EINVAL;
            const auto* socket = object->node()->unix_socket_capability();
            if (socket == nullptr)
                co_return -ENOTSOCK;
            u32  value = 0;
            long rc    = socket->getsockopt(*object, sqe.op_flags, sqe.aux, &value);
            if (rc < 0)
                co_return static_cast<i32>(rc);
            const u32 result_length = capacity < sizeof value ? capacity : sizeof value;
            if (!memory::copy_out_for(proc::current_process(),
                                      span<u8>::bytes(sqe.address, result_length),
                                      &value) ||
                !memory::copy_out_for(proc::current_process(), length.value.bytes(),
                                      &result_length))
                co_return -EFAULT;
            co_return 0;
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor getsockopt_descriptor = getsockopt::descriptor;

} // namespace astia::io::aio::operations

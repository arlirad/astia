#include "../../io/aio/handler.hpp"
#include "common.hpp"

namespace astia::io::aio::operations {

    struct getsockopt : handler<getsockopt, opcode::getsockopt> {
        static bool invoke([[maybe_unused]] ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
            auto object = file_for(sqe.fd);
            if (!object)
                return cq_post_guarded(ring, sqe.user_data, -EBADF);
            auto length = memory::user_ptr<u32>(sqe.offset).validate();
            if (!length || !memory::user_span_writable_ok_for(proc::current_process(),
                                                              length.value.bytes()))
                return cq_post_guarded(ring, sqe.user_data, -EFAULT);
            u32 capacity = 0;
            if (!memory::copy_in(&capacity, length.value.bytes()))
                return cq_post_guarded(ring, sqe.user_data, -EFAULT);
            if (capacity > max_operation_bytes)
                return cq_post_guarded(ring, sqe.user_data, -EINVAL);
            const auto* socket = object->node()->unix_socket_capability();
            if (socket == nullptr)
                return cq_post_guarded(ring, sqe.user_data, -ENOTSOCK);
            u32  value = 0;
            long rc    = socket->getsockopt(*object, sqe.op_flags, sqe.aux, &value);
            if (rc < 0)
                return cq_post_guarded(ring, sqe.user_data, static_cast<i32>(rc));
            const u32 result_length = capacity < sizeof value ? capacity : sizeof value;
            if (!memory::copy_out_for(proc::current_process(),
                                      span<u8>::bytes(sqe.address, result_length),
                                      &value) ||
                !memory::copy_out_for(proc::current_process(), length.value.bytes(),
                                      &result_length))
                return cq_post_guarded(ring, sqe.user_data, -EFAULT);
            return cq_post_guarded(ring, sqe.user_data, 0);
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor getsockopt_descriptor = getsockopt::descriptor;

} // namespace astia::io::aio::operations

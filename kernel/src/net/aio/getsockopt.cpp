#include "../../io/aio/handler.hpp"
#ifdef ASTIA_AIO_HOST_TEST
#include "dispatch.hpp"
#else
#include "common.hpp"
#endif

namespace astia::io::aio::operations {

#ifndef ASTIA_AIO_HOST_TEST
    namespace {

        bool handle_getsockopt(astia_rings* ring, const astia_sqe& s) {
            auto object = file_for(s.fd);
            if (!object)
                return cq_post_guarded(ring, s.user_data, -EBADF);
            auto length = memory::user_ptr<u32>(s.offset).validate();
            if (!length || !memory::user_span_writable_ok_for(proc::current_process(),
                                                              length.value.bytes()))
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            u32 capacity = 0;
            if (!memory::copy_in(&capacity, length.value.bytes()))
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            if (capacity > max_operation_bytes)
                return cq_post_guarded(ring, s.user_data, -EINVAL);
            const auto* socket = object->node()->unix_socket_capability();
            if (socket == nullptr)
                return cq_post_guarded(ring, s.user_data, -ENOTSOCK);
            u32  value = 0;
            long rc    = socket->getsockopt(*object, s.op_flags, s.aux, &value);
            if (rc < 0)
                return cq_post_guarded(ring, s.user_data, static_cast<i32>(rc));
            const u32 result_length = capacity < sizeof value ? capacity : sizeof value;
            if (!memory::copy_out_for(proc::current_process(),
                                      span<u8>::bytes(s.address, result_length),
                                      &value) ||
                !memory::copy_out_for(proc::current_process(), length.value.bytes(),
                                      &result_length))
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            return cq_post_guarded(ring, s.user_data, 0);
        }

    } // namespace
#endif

    struct getsockopt : handler<getsockopt, opcode::getsockopt> {
        static bool invoke([[maybe_unused]] ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
#ifdef ASTIA_AIO_HOST_TEST
            return ::astia::io::aio::socket::dispatch(slot, ring, sqe);
#else
            return handle_getsockopt(ring, sqe);
#endif
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor getsockopt_descriptor = getsockopt::descriptor;

} // namespace astia::io::aio::operations

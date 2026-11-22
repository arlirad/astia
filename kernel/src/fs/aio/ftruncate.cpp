#include "../../io/aio/handler.hpp"
#ifdef ASTIA_AIO_HOST_TEST
#include "dispatch.hpp"
#else
#include "common.hpp"
#endif

namespace astia::io::aio::operations {

#ifndef ASTIA_AIO_HOST_TEST
    namespace {

        bool handle_ftruncate(astia_rings* ring, const astia_sqe& s) {
            auto object = file_for(s.fd);
            if (!object)
                return cq_post_guarded(ring, s.user_data, -EBADF);
            if ((object->flags() & fs::open_file_access_mask) == 0)
                return cq_post_guarded(ring, s.user_data, -EBADF);
            return cq_post_guarded(ring, s.user_data,
                                   static_cast<i32>(object->truncate(s.offset)));
        }

    } // namespace
#endif

    struct ftruncate : handler<ftruncate, opcode::ftruncate> {
        static bool invoke([[maybe_unused]] ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
#ifdef ASTIA_AIO_HOST_TEST
            return ::astia::io::aio::filesystem::dispatch(slot, ring, sqe);
#else
            return handle_ftruncate(ring, sqe);
#endif
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor ftruncate_descriptor = ftruncate::descriptor;

} // namespace astia::io::aio::operations

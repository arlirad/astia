#include "../../io/aio/handler.hpp"
#include "common.hpp"

namespace astia::io::aio::operations {

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

    struct ftruncate : handler<ftruncate, opcode::ftruncate> {
        static bool invoke([[maybe_unused]] ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
            return handle_ftruncate(ring, sqe);
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor ftruncate_descriptor = ftruncate::descriptor;

} // namespace astia::io::aio::operations

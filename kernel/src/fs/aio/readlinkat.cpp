#include "../../io/aio/handler.hpp"
#ifdef ASTIA_AIO_HOST_TEST
#include "dispatch.hpp"
#else
#include "common.hpp"
#endif

namespace astia::io::aio::operations {

#ifndef ASTIA_AIO_HOST_TEST
    namespace {

        bool handle_readlinkat(astia_rings* ring, const astia_sqe& s) {
            char path[256];
            if (!memory::copy_in_string(span<char>(path, sizeof path),
                                        memory::user_ptr<char>(s.address)))
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            auto* current = proc::current_process();
            char  resolved[256];
            if (!current)
                return cq_post_guarded(ring, s.user_data, -ESRCH);
            long resolve_rc = resolve_at(current, s.fd, path, resolved, sizeof resolved);
            if (resolve_rc < 0)
                return cq_post_guarded(ring, s.user_data, (i32) resolve_rc);
            resolve_rc = fs::resolve_path(resolved, resolved, sizeof resolved, false);
            if (resolve_rc < 0)
                return cq_post_guarded(ring, s.user_data, (i32) resolve_rc);
            auto out = span<u8>::bytes(s.offset, s.length);
            if (!memory::user_span_ok_for(out))
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            // read_symlink is a kernel/VFS API; never give it a raw
            // caller address.  The fixed path limit is also the VFS's
            // maximum link length, so an oversized user capacity is
            // harmless and cannot become a kernel stack allocation.
            char       target[256]{};
            const u32  capacity = s.length < sizeof target ? s.length : sizeof target;
            const long n        = fs::read_symlink(resolved, target, capacity);
            if (n < 0)
                return cq_post_guarded(ring, s.user_data, (i32) n);
            if (n != 0 && !memory::copy_out(
                              span<u8>::bytes(s.offset, static_cast<usize>(n)), target))
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            return cq_post_guarded(ring, s.user_data, (i32) n);
        }

    } // namespace
#endif

    struct readlinkat : handler<readlinkat, opcode::readlinkat> {
        static bool invoke([[maybe_unused]] ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
#ifdef ASTIA_AIO_HOST_TEST
            return ::astia::io::aio::filesystem::dispatch(slot, ring, sqe);
#else
            return handle_readlinkat(ring, sqe);
#endif
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor readlinkat_descriptor = readlinkat::descriptor;

} // namespace astia::io::aio::operations

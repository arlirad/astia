#include "../../io/aio/handler.hpp"
#include "common.hpp"

namespace astia::io::aio::operations {
    struct readlinkat : handler<readlinkat, opcode::readlinkat> {
        static core::task<aio_result>
        invoke([[maybe_unused]] ring::ring_slot& slot, [[maybe_unused]] astia_rings* ring,
               astia_sqe sqe, [[maybe_unused]] core::cltoken token) noexcept {
            char path[fs::path_max];

            long copy_rc =
                copy_user_string(path, sizeof path, memory::user_ptr<char>(sqe.address));
            if (copy_rc < 0)
                co_return static_cast<i32>(copy_rc);

            auto* current = proc::current_process();

            char resolved[fs::path_max];

            if (!current)
                co_return -ESRCH;

            long resolve_rc =
                resolve_at(current, sqe.fd, path, resolved, sizeof resolved, false);
            if (resolve_rc < 0)
                co_return (i32) resolve_rc;

            auto out = span<u8>::bytes(sqe.offset, sqe.length);

            if (!memory::user_span_writable_ok(out))
                co_return -EFAULT;

            // read_symlink is a kernel/VFS API; never give it a raw
            // caller address.  The fixed path limit is also the VFS's
            // maximum link length, so an oversized user capacity is
            // harmless and cannot become a kernel stack allocation.
            char      target[fs::path_max]{};
            const u32 capacity =
                sqe.length < sizeof target ? sqe.length : static_cast<u32>(sizeof target);
            const long n = fs::read_symlink(resolved, target, capacity);
            if (n < 0)
                co_return (i32) n;

            if (n != 0 && !memory::copy_out(
                              span<u8>::bytes(sqe.offset, static_cast<usz>(n)), target))
                co_return -EFAULT;

            co_return (i32) n;
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor readlinkat_descriptor = readlinkat::descriptor;
} // namespace astia::io::aio::operations

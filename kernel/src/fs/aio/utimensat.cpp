#include "../../io/aio/handler.hpp"
#include "common.hpp"

namespace astia::io::aio::operations {

    struct utimensat : handler<utimensat, opcode::utimensat> {
        static core::task<aio_result>
        invoke([[maybe_unused]] ring::ring_slot& slot, [[maybe_unused]] astia_rings* ring,
               astia_sqe sqe, [[maybe_unused]] core::cltoken token) noexcept {
            char path[256];
            if (!memory::copy_in_string(span<char>(path, sizeof path),
                                        memory::user_ptr<char>(sqe.address)))
                co_return -EFAULT;
            auto* current = proc::current_process();
            char  resolved[256];
            if (current == nullptr)
                co_return -ESRCH;
            long resolve_rc = current->compose_path(path, resolved, sizeof resolved);
            if (resolve_rc >= 0)
                resolve_rc = fs::resolve_path(resolved, resolved, sizeof resolved, true);
            if (resolve_rc < 0)
                co_return (i32) resolve_rc;
            u64 sec  = 0;
            u64 nsec = 0;
            if (!memory::realtime_now(sec, nsec))
                co_return -EIO;
            co_return static_cast<i32>(fs::touch_path(resolved, sec, nsec));
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor utimensat_descriptor = utimensat::descriptor;

} // namespace astia::io::aio::operations

#include "../../io/aio/handler.hpp"
#include "common.hpp"

namespace astia::io::aio::operations {
    namespace {
        constexpr long nanoseconds_per_second = 1000000000L;

        struct requested_times {
            bool                set_atime  = false;
            bool                set_mtime  = false;
            bool                need_owner = false;
            bool                need_now   = false;
            fs::file_timestamps value{};
        };

        bool is_valid_nanoseconds(i64 nsec) {
            return nsec == ASTIA_UTIME_NOW || nsec == ASTIA_UTIME_OMIT ||
                   (nsec >= 0 && nsec < nanoseconds_per_second);
        }

        long add_time(const astia_timespec& source, bool atime, requested_times& times) {
            if (!is_valid_nanoseconds(source.tv_nsec))
                return -EINVAL;

            if (source.tv_nsec == ASTIA_UTIME_OMIT)
                return 0;

            if (atime)
                times.set_atime = true;
            else
                times.set_mtime = true;

            if (source.tv_nsec == ASTIA_UTIME_NOW) {
                times.need_now = true;

                return 0;
            }

            times.need_owner = true;

            if (atime) {
                times.value.atime_sec  = static_cast<u64>(source.tv_sec);
                times.value.atime_nsec = static_cast<u64>(source.tv_nsec);
            }
            else {
                times.value.mtime_sec  = static_cast<u64>(source.tv_sec);
                times.value.mtime_nsec = static_cast<u64>(source.tv_nsec);
            }

            return 0;
        }
    } // namespace

    struct utimensat : handler<utimensat, opcode::utimensat> {
        static core::task<aio_result>
        invoke([[maybe_unused]] ring::ring_slot& slot, [[maybe_unused]] astia_rings* ring,
               astia_sqe sqe, [[maybe_unused]] core::cltoken token) noexcept {
            char path[256];

            if (!memory::copy_in_string(span<char>(path, sizeof path),
                                        memory::user_ptr<char>(sqe.address)))
                co_return -EFAULT;

            auto* current = proc::current_process();
            if (current == nullptr)
                co_return -ESRCH;

            if ((sqe.op_flags & ~static_cast<u32>(AT_SYMLINK_NOFOLLOW)) != 0)
                co_return -EINVAL;

            const bool follow_final = (sqe.op_flags & AT_SYMLINK_NOFOLLOW) == 0;

            char resolved[256];

            const long resolve_rc = resolve_at(current, sqe.fd, path, resolved,
                                               sizeof resolved, follow_final);
            if (resolve_rc < 0)
                co_return (i32) resolve_rc;

            const fs::file_mode mode = fs::node_mode(resolved);

            if (!mode.exists())
                co_return -ENOENT;

            astia_timespec input[2] = {
                {0, ASTIA_UTIME_NOW},
                {0, ASTIA_UTIME_NOW},
            };

            if (sqe.offset != 0) {
                auto source = memory::user_ptr<astia_timespec>(sqe.offset).validate(2);
                if (!source)
                    co_return -EFAULT;

                if (!memory::copy_in(input, source.value.bytes(2)))
                    co_return -EFAULT;
            }

            requested_times requested;

            requested.value = fs::node_timestamps(resolved);

            long add_rc = add_time(input[0], true, requested);
            if (add_rc >= 0)
                add_rc = add_time(input[1], false, requested);

            if (add_rc < 0)
                co_return static_cast<i32>(add_rc);

            if (!requested.set_atime && !requested.set_mtime)
                co_return 0;

            if (requested.need_owner) {
                const uid_t owner = fs::node_uid(resolved);

                if (current->euid() != 0 && current->euid() != static_cast<u32>(owner))
                    co_return -EPERM;
            }
            else if (!owner_or_writable(current, resolved)) {
                co_return -EACCES;
            }

            if (requested.need_now) {
                u64 now_sec  = 0;
                u64 now_nsec = 0;

                if (!memory::realtime_now(now_sec, now_nsec))
                    co_return -EIO;

                if (requested.set_atime && input[0].tv_nsec == ASTIA_UTIME_NOW) {
                    requested.value.atime_sec  = now_sec;
                    requested.value.atime_nsec = now_nsec;
                }

                if (requested.set_mtime && input[1].tv_nsec == ASTIA_UTIME_NOW) {
                    requested.value.mtime_sec  = now_sec;
                    requested.value.mtime_nsec = now_nsec;
                }
            }

            co_return static_cast<i32>(
                fs::touch_path(resolved, requested.value, follow_final));
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor utimensat_descriptor = utimensat::descriptor;
} // namespace astia::io::aio::operations

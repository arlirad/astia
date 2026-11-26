#include <astia/api/errno.h>
#include <astia/api/syscalls.h>
#include <astia/crypto/csprng.hpp>
#include <astia/crypto/random.hpp>

#include "proc.hpp"
#include "user_mem.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    static_assert(crypto::csprng::max_request == ASTIA_GETRANDOM_MAX);

    struct getrandom : sys::syscall_handler<getrandom, sys::syscall_id::getrandom,
                                            sys::syscall_mode::sync> {
        static i64 invoke(sys::user_ptr<u8> buffer, u64 length, u32 flags) {
            if (current_process() == nullptr)
                return -ESRCH;

            // The pool never blocks today, so the Linux GRND_* flags have no
            // meaning yet; reject them instead of pretending to honour them.
            if (flags != 0 || length > ASTIA_GETRANDOM_MAX)
                return -EINVAL;

            if (length == 0)
                return 0;

            const auto destination = buffer.validate(static_cast<usize>(length));
            if (!destination)
                return -EFAULT;

            // Stage through kernel memory: the pool must not write through a
            // caller-owned address, even one that just validated.
            constexpr usize chunk_size = 128;
            u8              chunk[chunk_size];
            u64             done = 0;

            while (done < length) {
                const usize count =
                    length - done < chunk_size ? length - done : chunk_size;

                if (!crypto::random_bytes(chunk, count))
                    return done == 0 ? -EAGAIN : static_cast<i64>(done);

                const u64 address = reinterpret_cast<u64>(destination.value.get()) + done;

                if (!sys::copy_out(span<u8>::bytes(address, count), chunk))
                    return done == 0 ? -EFAULT : static_cast<i64>(done);

                done += count;
            }

            return static_cast<i64>(length);
        }
    };
} // namespace astia::proc::syscalls

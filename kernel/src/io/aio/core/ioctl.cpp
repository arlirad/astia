#include <astia/api/termios.h>

#include "../memory.hpp"
#include "common.hpp"

namespace astia::io::aio::operations {

    namespace {

        bool has_parked(const ring_slot& slot) {
            for (u32 i = 0; i < parked_operation_slots; ++i) {
                if (slot.park[i].used && !slot.park[i].done)
                    return true;
            }
            return false;
        }

        bool handle_ioctl(ring_slot& slot, astia_rings* ring, const astia_sqe& s) {
            /* ioctl is a control-plane fence.  Do not consume this SQE
               while an older read/write is parked: otherwise a batch
               containing READ, IOCTL, READ could observe tty state out
               of order.  Returning false leaves SQ head at IOCTL; the
               wake-up path retries it after the parked operation posts. */
            if (has_parked(slot))
                return false;
            // ioctl implementations expect a stable kernel object and
            // may both read and write it.  Decode the ABI's typed
            // requests here instead of passing a caller pointer into a
            // device (which was both a TOCTOU and kernel-pointer bug).
            usize expected = 0;
            bool  input = false, output = false;
            if (s.offset == TCGETS || s.offset == TCSETS || s.offset == TCSETSW ||
                s.offset == TCSETSF) {
                expected = sizeof(struct termios);
                input    = s.offset != TCGETS;
                output   = s.offset == TCGETS;
            }
            else if (s.offset == TCFLSH) {
                expected = sizeof(int);
                input    = true;
            }
            else if (s.offset == TIOCGWINSZ || s.offset == TIOCSWINSZ) {
                expected = sizeof(struct winsize);
                input    = s.offset == TIOCSWINSZ;
                output   = s.offset == TIOCGWINSZ;
            }
            else if (s.offset == TIOCGPGRP || s.offset == TIOCSPGRP ||
                     s.offset == TIOCGPTN || s.offset == TIOCSPTLCK ||
                     s.offset == TIOCGPTLCK) {
                expected = sizeof(i32);
                input    = s.offset == TIOCSPGRP || s.offset == TIOCSPTLCK;
                output   = s.offset == TIOCGPGRP || s.offset == TIOCGPTN ||
                         s.offset == TIOCGPTLCK;
            }
            else if (s.offset == TIOCSCTTY) {
                expected = 0;
            }
            else {
                return cq_post_guarded(ring, s.user_data, -ENOTTY);
            }
            const auto argument = memory::user_ptr<u8>(s.address).validate(expected);
            if (s.length != expected || !argument)
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            mm::shptr<fs::open_file> object = file_for(s.fd);
            if (!object)
                return cq_post_guarded(ring, s.user_data, -EBADF);
            __atomic_thread_fence(__ATOMIC_SEQ_CST);
            alignas(8) u8 ioctl_storage[sizeof(struct termios)]{};
            if (input && !memory::copy_in(ioctl_storage, argument.value.bytes(expected)))
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            void* kernel_arg =
                expected == 0 ? nullptr : static_cast<void*>(ioctl_storage);
            long rc = object->ioctl(s.offset, kernel_arg, expected);
            if (rc >= 0 && s.offset == TIOCSCTTY)
                rc = proc::acquire_controlling_terminal(object);
            if (rc >= 0 && output &&
                !memory::copy_out(argument.value.bytes(expected), ioctl_storage))
                return cq_post_guarded(ring, s.user_data, -EFAULT);
            __atomic_thread_fence(__ATOMIC_SEQ_CST);
            return cq_post_guarded(ring, s.user_data, static_cast<i32>(rc));
        }

    } // namespace

    struct ioctl : handler<ioctl, opcode::ioctl> {
        static bool invoke([[maybe_unused]] ring::ring_slot& slot, astia_rings* ring,
                           const astia_sqe& sqe) noexcept {
            return handle_ioctl(slot, ring, sqe);
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor ioctl_descriptor = ioctl::descriptor;

} // namespace astia::io::aio::operations

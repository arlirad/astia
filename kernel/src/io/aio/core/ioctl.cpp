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

    } // namespace

    struct ioctl : handler<ioctl, opcode::ioctl> {
        static core::task<aio_result> invoke([[maybe_unused]] ring::ring_slot& slot,
                                             [[maybe_unused]] astia_rings*     ring,
                                             const astia_sqe& sqe) noexcept {
            /* ioctl is a control-plane fence.  Do not consume this SQE
               while an older read/write is parked: otherwise a batch
               containing READ, IOCTL, READ could observe tty state out
               of order.  Returning false leaves SQ head at IOCTL; the
               wake-up path retries it after the parked operation posts. */
            if (has_parked(slot))
                co_return aio_result::retry;
            // ioctl implementations expect a stable kernel object and
            // may both read and write it.  Decode the ABI'sqe typed
            // requests here instead of passing a caller pointer into a
            // device (which was both a TOCTOU and kernel-pointer bug).
            usize expected = 0;
            bool  input = false, output = false;
            if (sqe.offset == TCGETS || sqe.offset == TCSETS || sqe.offset == TCSETSW ||
                sqe.offset == TCSETSF) {
                expected = sizeof(struct termios);
                input    = sqe.offset != TCGETS;
                output   = sqe.offset == TCGETS;
            }
            else if (sqe.offset == TCFLSH) {
                expected = sizeof(int);
                input    = true;
            }
            else if (sqe.offset == TIOCGWINSZ || sqe.offset == TIOCSWINSZ) {
                expected = sizeof(struct winsize);
                input    = sqe.offset == TIOCSWINSZ;
                output   = sqe.offset == TIOCGWINSZ;
            }
            else if (sqe.offset == TIOCGPGRP || sqe.offset == TIOCSPGRP ||
                     sqe.offset == TIOCGPTN || sqe.offset == TIOCSPTLCK ||
                     sqe.offset == TIOCGPTLCK) {
                expected = sizeof(i32);
                input    = sqe.offset == TIOCSPGRP || sqe.offset == TIOCSPTLCK;
                output   = sqe.offset == TIOCGPGRP || sqe.offset == TIOCGPTN ||
                         sqe.offset == TIOCGPTLCK;
            }
            else if (sqe.offset == TIOCSCTTY) {
                expected = 0;
            }
            else {
                co_return -ENOTTY;
            }
            const auto argument = memory::user_ptr<u8>(sqe.address).validate(expected);
            if (sqe.length != expected || !argument)
                co_return -EFAULT;
            mm::shptr<fs::open_file> object = file_for(sqe.fd);
            if (!object)
                co_return -EBADF;
            __atomic_thread_fence(__ATOMIC_SEQ_CST);
            alignas(8) u8 ioctl_storage[sizeof(struct termios)]{};
            if (input && !memory::copy_in(ioctl_storage, argument.value.bytes(expected)))
                co_return -EFAULT;
            void* kernel_arg =
                expected == 0 ? nullptr : static_cast<void*>(ioctl_storage);
            long rc = object->ioctl(sqe.offset, kernel_arg, expected);
            if (rc >= 0 && sqe.offset == TIOCSCTTY)
                rc = proc::acquire_controlling_terminal(object);
            if (rc >= 0 && output &&
                !memory::copy_out(argument.value.bytes(expected), ioctl_storage))
                co_return -EFAULT;
            __atomic_thread_fence(__ATOMIC_SEQ_CST);
            co_return static_cast<i32>(rc);
        }
    };

    [[gnu::used, gnu::section("astia_aio_handlers")]]
    extern const operation_descriptor ioctl_descriptor = ioctl::descriptor;

} // namespace astia::io::aio::operations

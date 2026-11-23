#include "syscall.hpp"

#include <astia/api/errno.h>
#include <astia/api/syscalls.h>

namespace astia::sys {

    namespace {
        syscall_exit_hook exit_hook = nullptr;
    }

    bool register_exit_hook(syscall_exit_hook hook) {
        if (hook == nullptr || exit_hook != nullptr) {
            return false;
        }
        exit_hook = hook;
        return true;
    }

    i64 dispatch(syscall_id id, u64 a0, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5,
                 arch::syscall_return& image) {
        i64 result = -ENOSYS;
        (void) production_dispatch(id, a0, a1, a2, a3, a4, a5, image, result);
        return result;
    }

} // namespace astia::sys

/// The arch entry's C door: the call number, the six argument registers, and
/// the return image the exit path will use.  The number and arguments travel
/// as ordinary parameters; the image carries only the caller's return state.
extern "C" astia::i64 astia_syscall_dispatch(astia::u64 nr, astia::u64 a0, astia::u64 a1,
                                       astia::u64 a2, astia::u64 a3, astia::u64 a4,
                                       astia::u64                   a5,
                                       astia::arch::syscall_return* image) {
    if (image == nullptr)
        return -EFAULT;

    const auto id =
        astia::mapper<astia::sys::syscall_id>::from(static_cast<astia::i64>(nr));
    if (!id)
        return -ENOSYS;

    /* The exit path starts as an ordinary return.  A handler or the exit
       hook may switch it to signal or sigreturn; the syscall result itself
       is returned to the entry separately. */
    image->mode = astia::arch::return_mode::ordinary;
    const auto result = astia::sys::dispatch(id.value, a0, a1, a2, a3, a4, a5, *image);

    if (astia::sys::exit_hook != nullptr) {
        astia::sys::exit_hook(*image, a0, a1, a2, a3, a4, a5, result);
    }

    return result;
}

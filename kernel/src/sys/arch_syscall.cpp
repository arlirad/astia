#include <astia/api/errno.h>
#include <astia/arch/context.hpp>
#include <astia/mapping.hpp>

#include "syscall.hpp"

using namespace astia;

/// The arch entry's C door: the call number, the six argument registers, and
/// the return image the exit path will use.  The number and arguments travel
/// as ordinary parameters; the image carries only the caller's return state.
extern "C" i64 astia_syscall_dispatch(u64 nr, u64 a0, u64 a1, u64 a2, u64 a3, u64 a4,
                                      u64 a5, arch::syscall_return* image) {
    if (image == nullptr)
        return -EFAULT;

    const auto id = mapper<sys::syscall_id>::from(static_cast<i64>(nr));
    if (!id)
        return -ENOSYS;

    /* The exit path starts as an ordinary return.  A handler or the exit hook
       may switch it to signal or sigreturn; the syscall result itself is
       returned to the entry separately. */
    image->set_mode(arch::return_mode::ordinary);

    const auto result = sys::dispatch(id.value, a0, a1, a2, a3, a4, a5, *image);
    sys::invoke_exit_hook(*image, result);

    return result;
}

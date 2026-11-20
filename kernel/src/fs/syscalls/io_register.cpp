#include "io_register.hpp"

#include <astia/api/errno.h>

#include <syscall.hpp>
namespace astia::fs::syscall {
    i64 io_register() {
        return -ENOSYS;
    }
}

namespace astia::fs::syscalls {

    /// io_register - Register asynchronous I/O resources.
    ///
    /// Synopsis:
    ///     long io_register(astia_rings *ring, uint32_t opcode,
    ///                      const void *ptr, uint32_t nr);
    ///
    /// Description:
    ///     Requests registration of resources for a ring. The ABI defines
    ///     REG_FILES and REG_BUFFERS, with UNREG_FILES and UNREG_BUFFERS for
    ///     removal. REG_FILES takes an array of int32_t file descriptors;
    ///     REG_BUFFERS takes an array of astia_reg_buf records containing an
    ///     address and byte length. Registered resources would be selected by
    ///     SQF_FIXED_FILE and SQF_FIXED_BUF in SQEs.
    ///
    /// Return Value:
    ///     This syscall always fails because resource registration is not
    ///     implemented. It does not pin files or buffers and does not change
    ///     the ring.
    ///
    /// Errors:
    ///     -ENOSYS: The requested registration or unregistration operation is unsupported
    ///     by the current kernel.
    ///
    /// Notes:
    ///     No registration opcode, pointer, count, or ring is currently
    ///     validated because the handler returns -ENOSYS before using them.
    /// ABI ID: io_register.
    /// Sync/async mode: async.
    /// Arguments: six ABI register words are available through syscall_context.
    /// Result: a signed i64 result or a negative errno.
    /// Ownership: fs subsystem.
    /// Errors: invalid or unavailable operations return the owning implementation's
    /// negative errno.
    struct io_register : sys::syscall_handler<io_register, sys::syscall_id::io_register,
                                              sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor io_register_descriptor = io_register::descriptor;

} // namespace astia::fs::syscalls

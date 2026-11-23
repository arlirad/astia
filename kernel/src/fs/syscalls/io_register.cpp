#include <astia/api/errno.h>

#include <syscall.hpp>

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
    struct io_register : sys::syscall_handler<io_register, sys::syscall_id::io_register,
                                              sys::syscall_mode::sync> {
        static i64 invoke() {
            return -ENOSYS;
        }
    };

} // namespace astia::fs::syscalls

#pragma once

#include <astia/common.hpp>

#include <syscall.hpp>
namespace astia::fs::syscall {
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
    i64 io_register();
}

namespace astia::fs::syscall_handlers {

    struct io_register_syscall
        : sys::syscall_handler<io_register_syscall, sys::syscall_id::io_register,
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

} // namespace astia::fs::syscall_handlers

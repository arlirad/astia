#include <astia/api/errno.h>
#include <astia/api/signal.h>
#include <astia/arch/context.hpp>
#include <astia/arch/cpu.hpp>
#include <astia/arch/layout.hpp>

#include "../../mapping.hpp"
#include "../../proc.hpp"
#include "../../scheduler.hpp"
#include "syscall.hpp"
#include "user_mem.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct sigreturn : sys::syscall_handler<sigreturn, sys::syscall_id::sigreturn,
                                            sys::syscall_mode::sync> {
        static i64 invoke(sys::syscall_trap trap, sys::user_ptr<u8> context) {
            constexpr usize capacity = arch::max_signal_context_bytes;

            alignas(16) u8 storage[capacity]{};

            const usize bytes = arch::signal_context_bytes();

            if (bytes == 0 || bytes > capacity || context.null())
                return -EFAULT;

            if (!sys::copy_in(storage, span<u8>::bytes(context.address(), bytes)))
                return -EFAULT;

            const auto state = arch::validate_signal_context(storage);

            if (!state.valid)
                return -EFAULT;

            if (state.faults) {
                if (auto* self = current_thread()) {
                    if (auto* owner = self->owner())
                        owner->signal_termination(signal_number::segv);

                    self->exit(reinterpret_cast<void*>(static_cast<usize>(
                        wait_signal_exit_status(static_cast<u8>(signal_number::segv)))));
                    __builtin_unreachable();
                }

                return -ESRCH;
            }

            const i64 resumed = trap.image.restore_signal(storage);

            if (auto* self = current_thread()) {
                self->signals().set_blocked(state.blocked);
                self->clear_signal_restore_mask();
                self->signal_stack_on_alt(state.alt_stack);
            }

            return resumed;
        }
    };
} // namespace astia::proc::syscalls

#include <astia/api/errno.h>
#include <astia/api/signal.h>
#include <astia/api/syscalls.h>
#include <astia/arch/context.hpp>
#include <astia/arch/cpu.hpp>
#include <astia/arch/interrupts.hpp>
#include <astia/log.hpp>
#include <astia/memory.hpp>
#include <astia/mm/heap.hpp>
#include <astia/wait.hpp>

#include "../ipc/futex.hpp"
#include "../ipc/mapping.hpp"
#include "mapping.hpp"
#include "proc.hpp"
#include "scheduler.hpp"
#include "signal_info.hpp"
#include "syscall.hpp"
#include "user_mem.hpp"

namespace astia::proc {
    namespace {
        /// This module's log source: the same one proc.cpp logs under.
        using logs = astia::log::context<"proc">;

        void deliver_default_signal(arch::syscall_return& image, i64 result) {
            auto* owner = current_process();
            auto* self  = current_thread();

            if (owner == nullptr || self == nullptr)
                return;

            constexpr usz capacity = arch::max_signal_context_bytes;

            alignas(16) u8 storage[capacity]{};

            const usz saved_bytes = arch::signal_context_bytes();

            if (owner->process_exit_requested() && self->exit_requested()) {
                self->exit(reinterpret_cast<void*>(
                    static_cast<usz>(owner->requested_exit_status())));
                __builtin_unreachable();
            }

            signal_disposition disposition{};
            signal_info        info{};
            signal_number      signal = self->signals().take_unblocked(nullptr, &info);

            if (valid_signal(signal))
                disposition = owner->signals().disposition(signal);
            else
                signal = owner->signals().take_unblocked_for(self->signals().blocked(),
                                                             &disposition, &info);

            if (!valid_signal(signal))
                return;

            const bool suspended = self->has_signal_restore_mask();
            const u32  restore_mask =
                suspended ? self->signal_restore_mask() : self->signals().blocked();

            if (disposition.action == signal_action::handler) {
                const usz bytes = saved_bytes + sizeof(astia_siginfo_t) + 8;
                const u64 stack = image.stack_pointer();

                if (stack < bytes || disposition.restorer == 0)
                    goto fatal_signal;

                const bool use_alt_stack = (disposition.flags & ASTIA_SA_ONSTACK) != 0 &&
                                           !self->signal_stack_on_alt() &&
                                           self->signal_stack_flags() != ASTIA_SS_DISABLE;
                u64 context = 0;

                if (use_alt_stack) {
                    const u64 top = self->signal_stack_sp() + self->signal_stack_size();

                    if (top < self->signal_stack_sp() || top < bytes)
                        goto fatal_signal;

                    context = (top - bytes) & ~u64{0xf};
                }
                else {
                    context = (stack - bytes) & ~u64{0xf};
                }

                const u64 info_address = context + saved_bytes;
                const u64 retaddr      = context - 8;

                if (!sys::user_span_ok(span<u8>::bytes(retaddr, bytes)) ||
                    !sys::user_span_ok(
                        span<u8>::bytes(reinterpret_cast<u64>(disposition.handler), 1)))
                    goto fatal_signal;

                const astia_siginfo_t user_info =
                    make_siginfo(info.signo, info.errno_value, info.code, info.pid,
                                 info.uid, info.status, info.addr);

                if (!sys::user_span_ok(
                        span<u8>::bytes(info_address, sizeof(user_info))) ||
                    !sys::copy_out(span<u8>::bytes(info_address, sizeof(user_info)),
                                   &user_info))
                    goto fatal_signal;

                if (saved_bytes > capacity)
                    goto fatal_signal;

                arch::capture_signal_context(storage, image, result, restore_mask,
                                             self->signal_stack_on_alt());

                if (!sys::copy_out(span<u8>::bytes(context, saved_bytes), storage) ||
                    !sys::copy_out(span<u8>::bytes(retaddr, 8), &disposition.restorer))
                    goto fatal_signal;

                if ((disposition.flags & ASTIA_SA_RESETHAND) != 0)
                    owner->signals().change_disposition(signal, {});

                const signal_mask automatic_mask =
                    (disposition.flags & ASTIA_SA_NODEFER) != 0 ? 0 : signal_bit(signal);

                self->signals().block(automatic_mask | disposition.mask);
                image.enter_signal(
                    reinterpret_cast<u64>(disposition.handler), retaddr,
                    static_cast<u64>(signal),
                    (disposition.flags & ASTIA_SA_SIGINFO) != 0 ? info_address : 0,
                    (disposition.flags & ASTIA_SA_SIGINFO) != 0 ? context : 0);

                if (use_alt_stack)
                    self->signal_stack_on_alt(true);

                return;
            }

            if (default_action(signal) == signal_default::terminate ||
                default_action(signal) == signal_default::core) {
                owner->terminate_for_signal(signal, self);
                self->exit(reinterpret_cast<void*>(
                    static_cast<usz>(wait_signal_exit_status(static_cast<u8>(signal)))));
                __builtin_unreachable();
            }

            if (default_action(signal) == signal_default::stop ||
                default_action(signal) == signal_default::continue_) {
                // Stop/continue transitions are applied synchronously by
                // kill/terminal group delivery.  Consuming the pending bit
                // here must not requeue SIGTSTP forever (or turn SIGCONT
                // into an ordinary ignored signal).
                if (suspended) {
                    self->signals().set_blocked(restore_mask);
                    self->clear_signal_restore_mask();
                }

                return;
            }

            // A default-ignored signal is consumed here.  Re-queueing it
            // would make every syscall exit deliver SIGCHLD forever when a
            // shell has no explicit SIGCHLD disposition.
            if (default_action(signal) == signal_default::ignore) {
                if (suspended) {
                    self->signals().set_blocked(restore_mask);
                    self->clear_signal_restore_mask();
                }

                return;
            }

            owner->signals().send(signal);

            return;
        fatal_signal:
            owner->terminate_for_signal(signal, self);
            self->exit(reinterpret_cast<void*>(
                static_cast<usz>(wait_signal_exit_status(static_cast<u8>(signal)))));
            __builtin_unreachable();
        }

        /* spawnve's three arguments cross as user pointers: a path
         * string and two NULL-terminated pointer vectors, each element
         * of a vector another string.  Everything lands in one kernel
         * block before proc::spawnve sees it.  The subsequent image load is
         * asynchronous, so the syscall may yield while the file is read;
         * unlike waitpid it never waits for the child to exit.  Bounds: 256
         * B per string, 32 elements per vector; past that the vectors are
         * rejected with -E2BIG rather than walked forever. */

        /// mmap — private memory in the caller's address space:
        /// anonymous, or a snapshot of an open file.  `addr` is a
        /// placement hint, never a forced address.  Never blocks.

        /// munmap — remove [addr, addr + len) from the caller's
        /// address space.  Never blocks.

        /// fork — duplicate the caller copy-on-write.  The one handler
        /// that takes the trap image itself instead of its six argument
        /// registers: the child resumes at the caller's own ring-3
        /// instruction, so proc::fork wants the caller's whole register
        /// state.  Never blocks.

        /// The #PF path's repair offer for ring-3 faults: the process
        /// resolves its copy-on-write pages (fork's shared mappings);
        /// everything it declines stays fatal.
        bool user_fault(u64 addr, bool write) {
            auto* owner = current_process();

            return owner != nullptr && owner->cow_fault(addr, write);
        }

        bool user_fault_signal(arch::interrupt_frame* raw_frame, u64 addr, u64 error) {
            auto* owner = current_process();
            auto* self  = current_thread();

            if (owner == nullptr || self == nullptr || raw_frame == nullptr)
                return false;

            constexpr auto signal       = signal_number::segv;
            const auto     fatal_signal = [&]() -> bool {
                owner->terminate_for_signal(signal, self);
                self->request_kill();
                return true;
            };
            const auto disposition = owner->signals().disposition(signal);

            if (disposition.action != signal_action::handler)
                return fatal_signal();

            constexpr usz capacity    = arch::max_signal_context_bytes;
            const usz     saved_bytes = arch::signal_context_bytes();

            if (saved_bytes == 0 || saved_bytes > capacity)
                return fatal_signal();

            alignas(16) u8 storage[capacity]{};

            const usz bytes = saved_bytes + sizeof(astia_siginfo_t) + 8;
            const u64 stack = arch::interrupt_stack_pointer(raw_frame);

            if (stack < bytes || disposition.restorer == 0)
                return fatal_signal();

            const bool use_alt_stack = (disposition.flags & ASTIA_SA_ONSTACK) != 0 &&
                                       !self->signal_stack_on_alt() &&
                                       self->signal_stack_flags() != ASTIA_SS_DISABLE;
            u64 context = 0;

            if (use_alt_stack) {
                const u64 top = self->signal_stack_sp() + self->signal_stack_size();

                if (top < self->signal_stack_sp() || top < bytes)
                    return fatal_signal();

                context = (top - bytes) & ~u64{0xf};
            }
            else {
                context = (stack - bytes) & ~u64{0xf};
            }

            const u64 info_address = context + saved_bytes;
            const u64 retaddr      = context - 8;

            if (!sys::user_span_ok(span<u8>::bytes(retaddr, bytes)) ||
                !sys::user_code_ptr_ok(reinterpret_cast<u64>(disposition.handler)))
                return fatal_signal();

            const i32 code = (error & 1) != 0 ? ASTIA_SEGV_ACCERR : ASTIA_SEGV_MAPERR;

            const astia_siginfo_t user_info =
                make_siginfo(static_cast<i32>(signal), 0, code, 0, 0, 0, addr);

            if (!sys::user_span_ok(span<u8>::bytes(info_address, sizeof(user_info))) ||
                !sys::copy_out(span<u8>::bytes(info_address, sizeof(user_info)),
                               &user_info))
                return fatal_signal();

            arch::capture_fault_signal_context(storage, *raw_frame,
                                               self->signals().blocked(),
                                               self->signal_stack_on_alt());

            if (!sys::copy_out(span<u8>::bytes(context, saved_bytes), storage) ||
                !sys::copy_out(span<u8>::bytes(retaddr, 8), &disposition.restorer))
                return fatal_signal();

            if ((disposition.flags & ASTIA_SA_RESETHAND) != 0)
                owner->signals().change_disposition(signal, {});

            const signal_mask automatic_mask =
                (disposition.flags & ASTIA_SA_NODEFER) != 0 ? 0 : signal_bit(signal);

            self->signals().block(automatic_mask | disposition.mask);
            arch::interrupt_enter_signal(
                raw_frame, reinterpret_cast<u64>(disposition.handler), retaddr,
                static_cast<u64>(signal),
                (disposition.flags & ASTIA_SA_SIGINFO) != 0 ? info_address : 0,
                (disposition.flags & ASTIA_SA_SIGINFO) != 0 ? context : 0);

            if (use_alt_stack)
                self->signal_stack_on_alt(true);

            return true;
        }
    }

    bool register_proc_hooks() {
        return sys::register_exit_hook(deliver_default_signal) &&
               arch::register_user_fault_handler(user_fault) &&
               arch::register_user_fault_signal_handler(user_fault_signal);
    }
} // namespace astia::proc

#include <astia/api/errno.h>
#include <astia/api/signal.h>
#include <astia/api/syscalls.h>
#include <astia/arch/context.hpp>
#include <astia/arch/cpu.hpp>
#include <astia/arch/interrupts.hpp>
#include <astia/log.hpp>
#include <astia/mm/heap.hpp>
#include <astia/wait.hpp>

#include "../ipc/futex.hpp"
#include "../ipc/mapping.hpp"
#include "mapping.hpp"
#include "proc.hpp"
#include "scheduler.hpp"
#include "syscall.hpp"
#include "user_mem.hpp"

namespace astia::proc {
    namespace {
        /// This module's log source: the same one proc.cpp logs under.
        using logs = astia::log::context<"proc">;

        void deliver_default_signal(arch::syscall_frame& raw_frame, i64 result) {
            auto* owner = current_process();
            auto* self  = current_thread();
            if (owner == nullptr || self == nullptr) {
                return;
            }
            if (owner->process_exit_requested() && self->exit_requested()) {
                self->exit(reinterpret_cast<void*>(
                    static_cast<usize>(owner->requested_exit_status())));
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
            if (!valid_signal(signal)) {
                return;
            }
            const bool suspended = self->has_signal_restore_mask();
            const u32  restore_mask =
                suspended ? self->signal_restore_mask() : self->signals().blocked();
            if (disposition.action == signal_action::handler) {
                auto&           frame = raw_frame;
                constexpr usize bytes =
                    sizeof(astia_x86_64_signal_context_t) + sizeof(astia_siginfo_t) + 8;
                if (frame.rsp < bytes || disposition.restorer == 0)
                    goto fatal_signal;
                const bool use_alt_stack = (disposition.flags & ASTIA_SA_ONSTACK) != 0 &&
                                           !self->signal_stack_on_alt() &&
                                           self->signal_stack_flags() != ASTIA_SS_DISABLE;
                u64        context       = 0;
                if (use_alt_stack) {
                    const u64 top = self->signal_stack_sp() + self->signal_stack_size();
                    if (top < self->signal_stack_sp() || top < bytes)
                        goto fatal_signal;
                    context = (top - bytes) & ~u64{0xf};
                }
                else {
                    context = (frame.rsp - bytes) & ~u64{0xf};
                }
                const u64 info_address = context + sizeof(astia_x86_64_signal_context_t);
                const u64 retaddr      = context - 8;
                if (!sys::user_span_ok(span<u8>::bytes(retaddr, bytes)) ||
                    !sys::user_span_ok(
                        span<u8>::bytes(reinterpret_cast<u64>(disposition.handler), 1)))
                    goto fatal_signal;
                astia_siginfo_t user_info{info.signo, info.errno_value, info.code,
                                          info.pid,   info.uid,         info.status,
                                          info.addr};
                if (!sys::user_span_ok(
                        span<u8>::bytes(info_address, sizeof(user_info))) ||
                    !sys::copy_out(span<u8>::bytes(info_address, sizeof(user_info)),
                                   &user_info))
                    goto fatal_signal;
                astia_x86_64_signal_context_t saved{static_cast<u64>(result),
                                                    frame.rbx,
                                                    frame.rip,
                                                    frame.a2,
                                                    frame.a1,
                                                    frame.a0,
                                                    frame.a4,
                                                    frame.a5,
                                                    frame.a3,
                                                    frame.rflags,
                                                    frame.r12,
                                                    frame.r13,
                                                    frame.r14,
                                                    frame.r15,
                                                    frame.rbp,
                                                    frame.rip,
                                                    frame.rsp,
                                                    frame.rflags,
                                                    restore_mask,
                                                    self->signal_stack_on_alt() ? 1u : 0u,
                                                    0};
                if (!sys::copy_out(span<u8>::bytes(context, sizeof saved), &saved) ||
                    !sys::copy_out(span<u8>::bytes(retaddr, 8), &disposition.restorer))
                    goto fatal_signal;
                if ((disposition.flags & ASTIA_SA_RESETHAND) != 0)
                    owner->signals().change_disposition(signal, {});
                const signal_mask automatic_mask =
                    (disposition.flags & ASTIA_SA_NODEFER) != 0 ? 0 : signal_bit(signal);
                self->signals().block(automatic_mask | disposition.mask);
                frame.nr = ~u64{0};
                frame.a0 = static_cast<u64>(signal);
                frame.a1 = (disposition.flags & ASTIA_SA_SIGINFO) != 0 ? info_address : 0;
                frame.a2 = (disposition.flags & ASTIA_SA_SIGINFO) != 0 ? context : 0;
                frame.a3 = frame.a4 = frame.a5 = 0;
                frame.rip = reinterpret_cast<u64>(disposition.handler);
                frame.rsp = retaddr;
                frame.rbx = frame.rbp = frame.r12 = frame.r13 = frame.r14 = frame.r15 = 0;
                if (use_alt_stack)
                    self->signal_stack_on_alt(true);
                return;
            }
            if (default_action(signal) == signal_default::terminate ||
                default_action(signal) == signal_default::core) {
                owner->signal_termination(signal);
                self->exit(reinterpret_cast<void*>(static_cast<usize>(
                    wait_signal_exit_status(static_cast<u8>(signal)))));
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
            owner->signal_termination(signal);
            self->exit(reinterpret_cast<void*>(
                static_cast<usize>(wait_signal_exit_status(static_cast<u8>(signal)))));
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
                owner->signal_termination(signal);
                self->request_kill();
                return true;
            };
            const auto disposition = owner->signals().disposition(signal);
            if (disposition.action != signal_action::handler) {
                return fatal_signal();
            }

            constexpr usize bytes =
                sizeof(astia_x86_64_signal_context_t) + sizeof(astia_siginfo_t) + 8;
            if (raw_frame->rsp < bytes || disposition.restorer == 0)
                return fatal_signal();
            const bool use_alt_stack = (disposition.flags & ASTIA_SA_ONSTACK) != 0 &&
                                       !self->signal_stack_on_alt() &&
                                       self->signal_stack_flags() != ASTIA_SS_DISABLE;
            u64        context       = 0;
            if (use_alt_stack) {
                const u64 top = self->signal_stack_sp() + self->signal_stack_size();
                if (top < self->signal_stack_sp() || top < bytes)
                    return fatal_signal();
                context = (top - bytes) & ~u64{0xf};
            }
            else {
                context = (raw_frame->rsp - bytes) & ~u64{0xf};
            }
            const u64 info_address = context + sizeof(astia_x86_64_signal_context_t);
            const u64 retaddr      = context - 8;
            if (!sys::user_span_ok(span<u8>::bytes(retaddr, bytes)) ||
                !sys::user_code_ptr_ok(reinterpret_cast<u64>(disposition.handler)))
                return fatal_signal();

            const i32 code = (error & 1) != 0 ? ASTIA_SEGV_ACCERR : ASTIA_SEGV_MAPERR;
            const astia_siginfo_t user_info{
                static_cast<i32>(signal), 0, code, 0, 0, 0, addr};
            if (!sys::user_span_ok(span<u8>::bytes(info_address, sizeof(user_info))) ||
                !sys::copy_out(span<u8>::bytes(info_address, sizeof(user_info)),
                               &user_info))
                return fatal_signal();

            const astia_x86_64_signal_context_t saved{
                raw_frame->rax,
                raw_frame->rbx,
                raw_frame->rcx,
                raw_frame->rdx,
                raw_frame->rsi,
                raw_frame->rdi,
                raw_frame->r8,
                raw_frame->r9,
                raw_frame->r10,
                raw_frame->r11,
                raw_frame->r12,
                raw_frame->r13,
                raw_frame->r14,
                raw_frame->r15,
                raw_frame->rbp,
                raw_frame->rip,
                raw_frame->rsp,
                raw_frame->rflags,
                self->signals().blocked(),
                static_cast<u32>(self->signal_stack_on_alt() ? 1u : 0u) | 2u,
                raw_frame->rip};
            if (!sys::copy_out(span<u8>::bytes(context, sizeof saved), &saved) ||
                !sys::copy_out(span<u8>::bytes(retaddr, 8), &disposition.restorer))
                return fatal_signal();
            if ((disposition.flags & ASTIA_SA_RESETHAND) != 0)
                owner->signals().change_disposition(signal, {});
            const signal_mask automatic_mask =
                (disposition.flags & ASTIA_SA_NODEFER) != 0 ? 0 : signal_bit(signal);
            self->signals().block(automatic_mask | disposition.mask);
            raw_frame->rdi = static_cast<u64>(signal);
            raw_frame->rsi =
                (disposition.flags & ASTIA_SA_SIGINFO) != 0 ? info_address : 0;
            raw_frame->rdx = (disposition.flags & ASTIA_SA_SIGINFO) != 0 ? context : 0;
            raw_frame->rip = reinterpret_cast<u64>(disposition.handler);
            raw_frame->rsp = retaddr;
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

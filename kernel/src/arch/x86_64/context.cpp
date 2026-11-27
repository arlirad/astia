#include <astia/api/signal.h>
#include <astia/arch/context.hpp>
#include <astia/arch/interrupt_frame.hpp>
#include <astia/arch/interrupts.hpp>
#include <astia/arch/layout.hpp>
#include <astia/arch/syscall_image.hpp>

#include "gdt.hpp"

namespace astia::arch {
    extern "C" void context_user_iret();
    extern "C" void astia_fork_iret();

    /// x86_64 cooperative kernel context: the callee-saved registers,
    /// stack pointer, and resume/entry instruction pointer.  This
    /// definition is intentionally private to the architecture port.
    /// The asm switch below uses only the fixed offsets 0..56.
    struct context {
        u64 rbx = 0;
        u64 rbp = 0;
        u64 r12 = 0;
        u64 r13 = 0;
        u64 r14 = 0;
        u64 r15 = 0;
        u64 rsp = 0;
        u64 rip = 0;
        // Ring-3 thread pointer (%fs base) for the process's static TLS.
        u64 fs_base = 0;

        // Ring-3 contexts own FP/SSE state — compilers emit SSE freely
        // under the x86-64 ABI — so switching between two of them must
        // swap the FXSAVE image too.  Kernel contexts never touch xmm
        // (-mgeneral-regs-only) and leave this alone.
        bool user = false;
        alignas(16) u8 fx[512]{};
    };

    extern "C" void astia_context_switch_raw(context*, const context*);

    void context_switch(context* old_context, const context* next_context) {
        // FXSAVE out / FXRSTOR in around the asm register switch.  The
        // next thread's FP state is loaded before the switch itself —
        // safe because kernel code carries no xmm/x87 use into that
        // window, and interrupts land in the same xmm-free kernel.
        if (old_context != nullptr && old_context->user) {
            asm volatile("fxsave64 %0" : "=m"(old_context->fx));
        }

        if (next_context != nullptr && next_context->user) {
            asm volatile("fxrstor64 %0" : : "m"(next_context->fx));

            // The kernel never touches %fs (-mgeneral-regs-only), so the
            // MSR carries whichever user thread ran last: load the
            // incoming thread's static-TLS thread pointer before the
            // switch takes us into its ring-3 code.  A context without
            // TLS zeroes it — no userspace code reaches %fs then.
            constexpr u32 ia32_fs_base = 0xC0000100u;
            const u32     low          = static_cast<u32>(next_context->fs_base);
            const u32     high         = static_cast<u32>(next_context->fs_base >> 32);

            asm volatile("wrmsr" : : "c"(ia32_fs_base), "a"(low), "d"(high) : "memory");
        }

        astia_context_switch_raw(old_context, next_context);
    }

    context* context_create() {
        return new context{};
    }

    void context_set_fs_base(context* value, u64 fs_base) {
        if (value != nullptr)
            value->fs_base = fs_base;

        constexpr u32 ia32_fs_base = 0xC0000100u;
        const u32     low          = static_cast<u32>(fs_base);
        const u32     high         = static_cast<u32>(fs_base >> 32);

        asm volatile("wrmsr" : : "c"(ia32_fs_base), "a"(low), "d"(high) : "memory");
    }

    bool context_prepare_kernel(context* value, u64 stack_top, u64 entry) {
        if (value == nullptr)
            return false;

        value->rsp = stack_top;
        value->rip = entry;

        return true;
    }

    bool context_prepare_user(context* value, u64 kernel_stack_top, u64 user_stack_top,
                              u64 entry, u64 fs_base) {
        if (value == nullptr || kernel_stack_top < 5 * sizeof(u64))
            return false;

        // context_switch jumps to the saved RIP.  The trampoline executes
        // iretq over this synthetic ring-3 frame.
        u64* frame = reinterpret_cast<u64*>(kernel_stack_top) - 5;

        frame[0]   = entry;
        frame[1]   = x86_64::user_code_selector;
        frame[2]   = 0x202; // IF + reserved bit
        frame[3]   = user_stack_top;
        frame[4]   = x86_64::user_data_selector;
        value->rsp = reinterpret_cast<u64>(frame);
        value->rip = reinterpret_cast<u64>(&context_user_iret);

        // Fresh-thread FP state: x87's default control word and MXCSR
        // with every exception masked.  FXRSTOR of an all-zero image
        // would unmask them all and kill the thread on the first
        // denormal.
        value->user    = true;
        value->fs_base = fs_base;

        const u16 fcw   = 0x037F;
        const u32 mxcsr = 0x1F80;

        __builtin_memset(value->fx, 0, sizeof value->fx);
        __builtin_memcpy(value->fx + 0, &fcw, sizeof fcw);
        __builtin_memcpy(value->fx + 24, &mxcsr, sizeof mxcsr);

        return true;
    }

    bool context_prepare_user_start(context* value, u64 kernel_stack_top,
                                    u64 user_stack_top, u64 entry, u64 fs_base,
                                    u64 argument0, u64 argument1) {
        if (!context_prepare_user(value, kernel_stack_top, user_stack_top - sizeof(u64),
                                  entry, fs_base))
            return false;

        // context_switch_raw restores r12 before entering the iretq
        // trampoline; user.S moves it into rdi.  r12 is caller-saved
        // by neither the kernel nor the initial C ABI entry, so this
        // does not leak a kernel pointer into the new thread.
        value->r12 = argument0;
        value->r13 = argument1;

        return true;
    }

    namespace {
        struct user_context_layout {
            u64 rip;
            u64 rflags;
            u64 rsp;
            u64 rbx;
            u64 rbp;
            u64 r12;
            u64 r13;
            u64 r14;
            u64 r15;
            u64 rdi;
            u64 rsi;
            u64 rdx;
            u64 r10;
            u64 r8;
            u64 r9;
            u64 fs_base;
        };

        static_assert(sizeof(user_context_layout) <= max_user_context_bytes);
    } // namespace

    usz user_context_bytes() {
        return sizeof(user_context_layout);
    }

    u64 user_context::stack_pointer() const noexcept {
        return static_cast<const user_context_layout*>(data())->rsp;
    }

    user_context caller_image(const syscall_return& image, u64 rdi, u64 rsi, u64 rdx,
                              u64 r10, u64 r8, u64 r9, u64 fs_base) {
        user_context result;

        auto& out = *static_cast<user_context_layout*>(result.data());

        out.rip     = image.raw_.rip;
        out.rflags  = image.raw_.rflags;
        out.rsp     = image.raw_.rsp;
        out.rbx     = image.raw_.rbx;
        out.rbp     = image.raw_.rbp;
        out.r12     = image.raw_.r12;
        out.r13     = image.raw_.r13;
        out.r14     = image.raw_.r14;
        out.r15     = image.raw_.r15;
        out.rdi     = rdi;
        out.rsi     = rsi;
        out.rdx     = rdx;
        out.r10     = r10;
        out.r8      = r8;
        out.r9      = r9;
        out.fs_base = fs_base;

        return result;
    }

    namespace {
        static_assert(sizeof(astia_x86_64_signal_context_t) <= max_signal_context_bytes);

        constexpr u64 user_interrupt_flag = 1u << 9;

        const astia_x86_64_signal_context_t& saved_context(const void* storage) {
            return *static_cast<const astia_x86_64_signal_context_t*>(storage);
        }

        astia_x86_64_signal_context_t& saved_context(void* storage) {
            return *static_cast<astia_x86_64_signal_context_t*>(storage);
        }
    } // namespace

    usz signal_context_bytes() {
        return sizeof(astia_x86_64_signal_context_t);
    }

    i64 syscall_return::restore_signal(const void* storage) noexcept {
        const auto& saved = saved_context(storage);

        raw_.mode   = static_cast<u64>(return_mode::sigreturn);
        raw_.rdi    = saved.rdi;
        raw_.rsi    = saved.rsi;
        raw_.rdx    = saved.rdx;
        raw_.r10    = saved.r10;
        raw_.r8     = saved.r8;
        raw_.r9     = saved.r9;
        raw_.rip    = saved.rip;
        raw_.rflags = saved.rflags;
        raw_.rsp    = saved.rsp;
        raw_.rbx    = saved.rbx;
        raw_.rbp    = saved.rbp;
        raw_.r12    = saved.r12;
        raw_.r13    = saved.r13;
        raw_.r14    = saved.r14;
        raw_.r15    = saved.r15;

        return static_cast<i64>(saved.rax);
    }

    void capture_signal_context(void* storage, const syscall_return& image, i64 result,
                                u32 blocked, bool alt_stack) {
        auto&                         out = saved_context(storage);
        const syscall_return::layout& in  = image.raw_;

        out.rax       = static_cast<u64>(result);
        out.rbx       = in.rbx;
        out.rcx       = in.rip;
        out.rdx       = in.rdx;
        out.rsi       = in.rsi;
        out.rdi       = in.rdi;
        out.r8        = in.r8;
        out.r9        = in.r9;
        out.r10       = in.r10;
        out.r11       = in.rflags;
        out.r12       = in.r12;
        out.r13       = in.r13;
        out.r14       = in.r14;
        out.r15       = in.r15;
        out.rbp       = in.rbp;
        out.rip       = in.rip;
        out.rsp       = in.rsp;
        out.rflags    = in.rflags;
        out.blocked   = blocked;
        out.reserved  = alt_stack ? 1u : 0u;
        out.fault_rip = 0;
    }

    void capture_fault_signal_context(void* storage, const interrupt_frame& frame,
                                      u32 blocked, bool alt_stack) {
        auto& out = saved_context(storage);

        out.rax       = frame.rax;
        out.rbx       = frame.rbx;
        out.rcx       = frame.rcx;
        out.rdx       = frame.rdx;
        out.rsi       = frame.rsi;
        out.rdi       = frame.rdi;
        out.r8        = frame.r8;
        out.r9        = frame.r9;
        out.r10       = frame.r10;
        out.r11       = frame.r11;
        out.r12       = frame.r12;
        out.r13       = frame.r13;
        out.r14       = frame.r14;
        out.r15       = frame.r15;
        out.rbp       = frame.rbp;
        out.rip       = frame.rip;
        out.rsp       = frame.rsp;
        out.rflags    = frame.rflags;
        out.blocked   = blocked;
        out.reserved  = (alt_stack ? 1u : 0u) | 2u;
        out.fault_rip = frame.rip;
    }

    signal_restore_state validate_signal_context(const void* storage) {
        const auto& saved = saved_context(storage);

        signal_restore_state state;

        state.valid = saved.rip != 0 && saved.rsp != 0 && saved.rip < kernel_base &&
                      saved.rsp < kernel_base &&
                      (saved.rflags & user_interrupt_flag) != 0;
        state.faults    = (saved.reserved & 2u) != 0 && saved.rip == saved.fault_rip;
        state.blocked   = saved.blocked;
        state.alt_stack = (saved.reserved & 1u) != 0;

        return state;
    }

    bool context_prepare_fork(context* value, u64 kernel_stack_top,
                              const user_context* image) {
        if (value == nullptr || image == nullptr || kernel_stack_top < 11 * sizeof(u64))
            return false;

        // The iretq frame the resume trampoline pops: the parent's
        // return point, flags, and stack — the child continues the
        // fork() caller as if nothing had happened, with rax = 0.
        const auto& in = *static_cast<const user_context_layout*>(image->data());

        u64* frame = reinterpret_cast<u64*>(kernel_stack_top) - 5;

        // Keep the syscall-preserved argument registers alongside the iret
        // frame.  They cannot live in the context object: the raw switch
        // deliberately exposes only the fixed callee-saved ABI, and the
        // trampoline has no context pointer after the switch.
        frame[-6]  = in.rdi;
        frame[-5]  = in.rsi;
        frame[-4]  = in.rdx;
        frame[-3]  = in.r10;
        frame[-2]  = in.r8;
        frame[-1]  = in.r9;
        frame[0]   = in.rip;
        frame[1]   = x86_64::user_code_selector;
        frame[2]   = in.rflags;
        frame[3]   = in.rsp;
        frame[4]   = x86_64::user_data_selector;
        value->rsp = reinterpret_cast<u64>(frame);
        value->rip = reinterpret_cast<u64>(&astia_fork_iret);

        // The raw switch restores these from the context, so the
        // parent's callee-saved image travels through it untouched.
        value->rbx = in.rbx;
        value->rbp = in.rbp;
        value->r12 = in.r12;
        value->r13 = in.r13;
        value->r14 = in.r14;
        value->r15 = in.r15;

        value->user    = true;
        value->fs_base = in.fs_base;

        // The kernel ran the parent's syscall without touching xmm
        // (-mgeneral-regs-only), so the live FP/SSE state IS the
        // parent's — snapshot it so the child inherits it (POSIX
        // forks register state, not just memory).
        asm volatile("fxsave64 %0" : "=m"(value->fx));

        return true;
    }

    void context_destroy(context* value) {
        delete value;
    }
} // namespace astia::arch

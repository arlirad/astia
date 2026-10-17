#include "gdt.hpp"
#include <astia/arch/context.hpp>

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

    bool context_prepare_kernel(context* value, u64 stack_top, u64 entry) {
        if (value == nullptr) {
            return false;
        }
        value->rsp = stack_top;
        value->rip = entry;
        return true;
    }

    bool context_prepare_user(context* value, u64 kernel_stack_top, u64 user_stack_top,
                              u64 entry, u64 fs_base) {
        if (value == nullptr || kernel_stack_top < 5 * sizeof(u64)) {
            return false;
        }
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
        value->user     = true;
        value->fs_base  = fs_base;
        const u16 fcw   = 0x037F;
        const u32 mxcsr = 0x1F80;
        __builtin_memset(value->fx, 0, sizeof value->fx);
        __builtin_memcpy(value->fx + 0, &fcw, sizeof fcw);
        __builtin_memcpy(value->fx + 24, &mxcsr, sizeof mxcsr);
        return true;
    }

    bool context_prepare_user_start(context* value, u64 kernel_stack_top,
                                    u64 user_stack_top, u64 entry, u64 fs_base,
                                    u64 argument) {
        if (!context_prepare_user(value, kernel_stack_top, user_stack_top, entry,
                                  fs_base)) {
            return false;
        }
        // context_switch_raw restores r12 before entering the iretq
        // trampoline; user.S moves it into rdi.  r12 is caller-saved
        // by neither the kernel nor the initial C ABI entry, so this
        // does not leak a kernel pointer into the new thread.
        value->r12 = argument;
        return true;
    }

    user_context caller_image(const syscall_frame& frame) {
        user_context image;
        image.rip    = frame.rip;
        image.rflags = frame.rflags;
        image.rsp    = frame.rsp;
        image.rbx    = frame.rbx;
        image.rbp    = frame.rbp;
        image.r12    = frame.r12;
        image.r13    = frame.r13;
        image.r14    = frame.r14;
        image.r15    = frame.r15;
        return image;
    }

    bool context_prepare_fork(context* value, u64 kernel_stack_top,
                              const user_context* image) {
        if (value == nullptr || image == nullptr || kernel_stack_top < 5 * sizeof(u64)) {
            return false;
        }
        // The iretq frame the resume trampoline pops: the parent's
        // return point, flags, and stack — the child continues the
        // fork() caller as if nothing had happened, with rax = 0.
        u64* frame = reinterpret_cast<u64*>(kernel_stack_top) - 5;
        frame[0]   = image->rip;
        frame[1]   = x86_64::user_code_selector;
        frame[2]   = image->rflags;
        frame[3]   = image->rsp;
        frame[4]   = x86_64::user_data_selector;
        value->rsp = reinterpret_cast<u64>(frame);
        value->rip = reinterpret_cast<u64>(&astia_fork_iret);

        // The raw switch restores these from the context, so the
        // parent's callee-saved image travels through it untouched.
        value->rbx = image->rbx;
        value->rbp = image->rbp;
        value->r12 = image->r12;
        value->r13 = image->r13;
        value->r14 = image->r14;
        value->r15 = image->r15;

        value->user    = true;
        value->fs_base = image->fs_base;

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

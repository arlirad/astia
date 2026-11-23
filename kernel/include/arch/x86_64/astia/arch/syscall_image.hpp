#pragma once

#include <astia/common.hpp>

#include <concepts>

namespace astia::arch {

    class user_context;

    /// Selects what the exit path does with the return image.  These are
    /// return paths, not error codes: the ordinary result travels separately
    /// in the register the C door returns to the entry.
    enum class return_mode : u64 {
        /// Restore the interrupted user context as-is.
        ordinary = 0,
        /// Deliver a pending signal before returning to ring 3.
        signal = 1,
        /// Restore the saved image from sigreturn.
        sigreturn = 2,
    };

    /// Return-side register image of a ring-3 caller.  The register data is
    /// private: generic code only selects a return path or asks the port to
    /// adopt a signal context.  The entry owns the byte layout, asserted
    /// below against the asm contract.
    ///
    /// The syscall number and argument registers are not part of this image;
    /// the entry hands those to the dispatcher directly.
    class syscall_return {
        public:
        static constexpr usize bytes = 16 * sizeof(u64);

        void set_mode(return_mode mode) noexcept {
            raw_.mode = static_cast<u64>(mode);
        }
        return_mode mode() const noexcept {
            return static_cast<return_mode>(raw_.mode);
        }

        /// Stack pointer the interrupted context will resume on.
        u64 stack_pointer() const noexcept {
            return raw_.rsp;
        }

        /// Redirect the exit path to a new user image (execve).
        void set_resume_point(u64 rip, u64 rsp, u64 rflags) noexcept {
            raw_.rip    = rip;
            raw_.rsp    = rsp;
            raw_.rflags = rflags;
        }

        /// Redirect the exit path at `handler` on `stack` with the signal
        /// handler's arguments, discarding the interrupted callee-saved image.
        void enter_signal(u64 handler, u64 stack, u64 signo, u64 info,
                          u64 context) noexcept {
            raw_.mode = static_cast<u64>(return_mode::signal);
            raw_.rdi  = signo;
            raw_.rsi  = info;
            raw_.rdx  = context;
            raw_.r10  = 0;
            raw_.r8   = 0;
            raw_.r9   = 0;
            raw_.rip  = handler;
            raw_.rsp  = stack;
            raw_.rbx  = 0;
            raw_.rbp  = 0;
            raw_.r12  = 0;
            raw_.r13  = 0;
            raw_.r14  = 0;
            raw_.r15  = 0;
        }

        /// Replace the image from an opaque saved signal context (as produced
        /// by `capture_signal_context`) and mark the exit path as a
        /// sigreturn.  Returns the resumed return value.
        i64 restore_signal(const void* storage) noexcept;

        private:
        /// The asm contract.  mode is at offset zero because the entry tests
        /// it there; the rest is the caller-saved image, the ring-3 control
        /// state, and the callee-saved image.
        struct layout {
            u64 mode;
            u64 rdi, rsi, rdx, r10, r8, r9;
            u64 rip, rflags, rsp;
            u64 rbx, rbp, r12, r13, r14, r15;
        };
        static_assert(sizeof(layout) == bytes);
        static_assert(__builtin_offsetof(layout, mode) == 0);
        static_assert(__builtin_offsetof(layout, rip) == 56);
        static_assert(__builtin_offsetof(layout, r15) == 120);

        layout raw_{};

        friend user_context caller_image(const syscall_return& image, u64 rdi, u64 rsi,
                                         u64 rdx, u64 r10, u64 r8, u64 r9, u64 fs_base);
        friend void capture_signal_context(void* storage, const syscall_return& image,
                                           i64 result, u32 blocked, bool alt_stack);
    };

    static_assert(sizeof(syscall_return) == syscall_return::bytes);

    /// Operations the generic exit path needs from the port's return image.
    template <typename Image>
    concept syscall_return_image = requires(Image image) {
        { image.set_mode(return_mode::ordinary) };
        { image.mode() } -> std::same_as<return_mode>;
        { image.stack_pointer() } -> std::convertible_to<u64>;
        { image.set_resume_point(u64{}, u64{}, u64{}) };
        { image.enter_signal(u64{}, u64{}, u64{}, u64{}, u64{}) };
        { image.restore_signal(static_cast<const void*>(nullptr)) } -> std::same_as<i64>;
    };

    static_assert(syscall_return_image<syscall_return>);

} // namespace astia::arch

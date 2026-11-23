#pragma once

#include <astia/arch/signal.hpp>
#include <astia/arch/syscall_image.hpp>
#include <astia/common.hpp>

#include <concepts>

namespace astia::arch {

    /// Opaque architecture-owned execution context.  The selected
    /// architecture defines its register layout and allocates it.
    struct context;
    void context_set_fs_base(context* value, u64 fs_base);

    /// Upper bound on a port's resumable user-context image.  Generic code
    /// owns an opaque buffer of this size; the port asserts its own layout
    /// fits, so no generic TU needs the register layout.
    inline constexpr usize max_user_context_bytes = 256;

    /// Exact size of the port's resumable user-context image.
    usize user_context_bytes();

    /// Register image of a ring-3 caller at syscall time.  The arch
    /// syscall entry packs it from its frame; fork hands it to
    /// context_prepare_fork so the child resumes exactly where the
    /// parent sat.  The storage is opaque; generic code only reads the
    /// stack pointer.
    class user_context {
        public:
        void* data() noexcept {
            return storage_;
        }
        const void* data() const noexcept {
            return storage_;
        }
        u64 stack_pointer() const noexcept;

        private:
        alignas(16) u8 storage_[max_user_context_bytes]{};
    };

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

    /// The caller's own state out of a syscall return image and the six
    /// argument registers captured at entry, in the form a context is
    /// prepared from: what fork hands the child so it returns from the
    /// syscall exactly where the parent sat.
    user_context caller_image(const syscall_return& image, u64 rdi, u64 rsi, u64 rdx,
                              u64 r10, u64 r8, u64 r9, u64 fs_base);

    context* context_create();
    bool     context_prepare_kernel(context*, u64 stack_top, u64 entry);
    bool     context_prepare_user(context*, u64 kernel_stack_top, u64 user_stack_top,
                                  u64 entry, u64 fs_base);
    /// Prepare a new user thread at `entry` with its first argument in
    /// the ABI's %rdi.  The architecture uses a callee-saved staging
    /// register while entering the synthetic iretq frame.
    bool context_prepare_user_start(context*, u64 kernel_stack_top, u64 user_stack_top,
                                    u64 entry, u64 fs_base, u64 argument0, u64 argument1);
    /// Prepare `value` to enter ring 3 exactly where a forked parent
    /// sat inside its syscall: the register image, thread pointer, and
    /// FP/SSE state are the parent's, and the child's fork() return value
    /// reads 0.  The caller supplies the kernel stack the
    /// resume trampoline builds its iretq frame on.
    bool context_prepare_fork(context*, u64 kernel_stack_top, const user_context* image);
    void context_destroy(context*);

    extern "C" void context_switch(context* old_context, const context* next_context);

} // namespace astia::arch

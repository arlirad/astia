#pragma once

#include <astia/api/errno.h>
#include <astia/arch/context.hpp>
#include <astia/arch/syscall_image.hpp>
#include <astia/common.hpp>
#include <astia/core/completion.hpp>
#include <astia/core/task.hpp>
#include <astia/mapping.hpp>
#include <astia/resource_limits.hpp>

#include <type_traits>

namespace astia::sys {

    inline constexpr u32 syscall_table_capacity =
        resource_limits::abi::syscall_table_capacity;

    /// Architecture-independent syscall numbers mirror the public ABI.
    enum class syscall_id : u32 {
        io_setup = 0,
        io_destroy,
        io_submit,
        io_register,
        mmap,
        munmap,
        mprotect,
        clock_gettime,
        exit,
        yield,
        getpid,
        gettid,
        futex,
        waitpid,
        spawnve,
        fork,
        thread_create,
        thread_exit,
        thread_join,
        thread_detach,
        kill,
        sigaction,
        sigprocmask,
        thread_create2,
        execve,
        sigsuspend,
        getpgrp,
        setpgid,
        setsid,
        sigreturn,
        chdir,
        getcwd,
        getuid,
        geteuid,
        getgid,
        getegid,
        setuid,
        seteuid,
        setgid,
        setegid,
        setreuid,
        setregid,
        getgroups,
        setgroups,
        process_snapshot,
        getsid,
        unmount,
        mount,
        getrlimit,
        setrlimit,
        sigwait,
        sigaltstack,
        getpgid,
        getppid,
        thread_cancel         = 54,
        alarm                 = 55,
        thread_create_owned   = 56,
        thread_cancel_pending = 57,
        thread_exists         = 58,
        thread_kill           = 59,
        sigpending            = 60,
        sigtimedwait          = 61,
        clock_getres          = 62,
        thread_getattr        = 63,
        msync                 = 64,
        lseek                 = 65,
        umask                 = 66,
        shm_open              = 67,
        shm_unlink            = 68,
        fchdir                = 69,
        uname                 = 70,
    };

    /// How many registers the ABI hands every trap — and so the most
    /// arguments any handler can be given.
    inline constexpr u32 syscall_argument_count = 6;

    /// The trap state for the three handlers that need more than their
    /// decoded arguments: fork builds the child's resume image from the
    /// caller-saved registers, execve rewrites the resume point, and
    /// sigreturn replaces the whole return image.  `arguments` is the same
    /// six registers the dispatcher decodes, borrowed for the call.
    struct syscall_trap {
        const u64 (&arguments)[syscall_argument_count];
        arch::syscall_return& image;
    };

    using syscall_exit_hook = void (*)(arch::syscall_return& image, i64 result);

    using syscall_handler_fn = void (*)(u64 a0, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5,
                                        arch::syscall_return&  image,
                                        core::completion<i64>& completion);

    struct syscall_descriptor {
        syscall_id         id;
        syscall_handler_fn invoke;
    };

    bool register_exit_hook(syscall_exit_hook hook);

    /// Run the registered exit hook, if any.  The arch entry door calls this
    /// after dispatch; the hook pointer itself stays private to syscall.cpp.
    void invoke_exit_hook(arch::syscall_return& image, i64 result);

    namespace detail {

        template <usize... I>
        struct index_sequence {};

        template <usize N, usize... I>
        struct make_index_sequence : make_index_sequence<N - 1, N - 1, I...> {};

        template <usize... I>
        struct make_index_sequence<0, I...> {
            using type = index_sequence<I...>;
        };

        /// Decode one ABI register into a handler parameter.  The primary
        /// template covers scalar widths; semantic argument types specialize
        /// it (see user_mem.hpp for validated user pointers).
        template <typename T>
        struct argument_decoder {
            static bool valid([[maybe_unused]] u64 raw) {
                return true;
            }

            static T decode(u64 raw) {
                return static_cast<T>(raw);
            }
        };

        template <typename T>
        struct is_trap_argument : std::false_type {};

        template <>
        struct is_trap_argument<syscall_trap> : std::true_type {};

        template <typename First, typename... Rest>
        inline constexpr usize abi_argument_count =
            sizeof...(Rest) +
            (is_trap_argument<std::remove_cvref_t<First>>::value ? 0 : 1);

        template <typename... Args>
        struct valid_typed_arguments : std::false_type {};

        template <>
        struct valid_typed_arguments<> : std::true_type {};

        template <typename First, typename... Rest>
        struct valid_typed_arguments<First, Rest...>
            : std::bool_constant<
                  (!is_trap_argument<std::remove_cvref_t<First>>::value ||
                   std::is_same_v<First, syscall_trap>) &&
                  ((!is_trap_argument<std::remove_cvref_t<Rest>>::value) && ...) &&
                  (abi_argument_count<First, Rest...> <= syscall_argument_count)> {};

        template <typename Fn>
        struct typed_sync_handler : std::false_type {};

        template <typename... Args>
        struct typed_sync_handler<i64 (*)(Args...)> : valid_typed_arguments<Args...> {};

        template <typename Fn>
        struct typed_task_handler : std::false_type {};

        template <typename... Args>
        struct typed_task_handler<core::task<i64> (*)(Args...)>
            : valid_typed_arguments<Args...> {};

        template <typename... Args, usize... I>
        bool valid_arguments(const u64 (&arguments)[syscall_argument_count],
                             index_sequence<I...>) {
            return (argument_decoder<Args>::valid(arguments[I]) && ...);
        }

        /// Decode the register arguments and invoke a handler.  A handler may
        /// lead with `syscall_trap` to receive the trap image; that parameter
        /// is not an ABI argument and does not consume a register.
        template <typename Fn>
        struct typed_invoker;

        template <typename R, typename... Args>
        struct typed_invoker<R (*)(Args...)> {
            static R call(R (*function)(Args...),
                          const u64 (&arguments)[syscall_argument_count],
                          [[maybe_unused]] arch::syscall_return& image) {
                const auto indices =
                    typename make_index_sequence<sizeof...(Args)>::type{};
                return call_arguments(function, arguments, indices);
            }

            private:
            template <usize... I>
            static R call_arguments(R (*function)(Args...),
                                    const u64 (&arguments)[syscall_argument_count],
                                    index_sequence<I...>) {
                return function(argument_decoder<Args>::decode(arguments[I])...);
            }
        };

        template <typename R, typename... Args>
        struct typed_invoker<R (*)(syscall_trap, Args...)> {
            static R call(R (*function)(syscall_trap, Args...),
                          const u64 (&arguments)[syscall_argument_count],
                          arch::syscall_return& image) {
                const auto indices =
                    typename make_index_sequence<sizeof...(Args)>::type{};
                syscall_trap trap{arguments, image};
                return call_arguments(function, trap, arguments, indices);
            }

            private:
            template <usize... I>
            static R call_arguments(R (*function)(syscall_trap, Args...),
                                    syscall_trap trap,
                                    const u64 (&arguments)[syscall_argument_count],
                                    index_sequence<I...>) {
                return function(trap, argument_decoder<Args>::decode(arguments[I])...);
            }
        };

        /// Whether every semantic argument accepts the register values.  The
        /// leading trap parameter, when present, is not decoded.
        template <typename Fn>
        struct decoded_arguments_valid;

        template <typename R, typename... Args>
        struct decoded_arguments_valid<R (*)(Args...)> {
            static bool check(const u64 (&arguments)[syscall_argument_count]) {
                const auto indices =
                    typename make_index_sequence<sizeof...(Args)>::type{};
                return valid_arguments<Args...>(arguments, indices);
            }
        };

        template <typename R, typename... Args>
        struct decoded_arguments_valid<R (*)(syscall_trap, Args...)> {
            static bool check(const u64 (&arguments)[syscall_argument_count]) {
                const auto indices =
                    typename make_index_sequence<sizeof...(Args)>::type{};
                return valid_arguments<Args...>(arguments, indices);
            }
        };

    } // namespace detail

    enum class syscall_mode : u8 {
        sync,
        async,
    };

    template <class Impl, syscall_id Id, syscall_mode Mode>
    struct syscall_handler;

    template <typename Impl, syscall_id Id>
    struct syscall_handler<Impl, Id, syscall_mode::sync> {
        using syscall_descriptor_marker = void;

        static constexpr syscall_id id = Id;
        static void                 thunk(u64 a0, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5,
                                          arch::syscall_return&  image,
                                          core::completion<i64>& completion) {
            using function = decltype(&Impl::invoke);
            static_assert(
                detail::typed_sync_handler<function>::value,
                "sync syscall handlers must return i64 from invoke(typed arguments)");
            const u64 arguments[syscall_argument_count]{a0, a1, a2, a3, a4, a5};
            if (!detail::decoded_arguments_valid<function>::check(arguments)) {
                completion.complete(-EFAULT);
                return;
            }
            completion.complete(static_cast<i64>(
                detail::typed_invoker<function>::call(&Impl::invoke, arguments, image)));
        }

        static constexpr syscall_descriptor descriptor{Id, &thunk};
    };

    template <typename Impl, syscall_id Id>
    struct syscall_handler<Impl, Id, syscall_mode::async> {
        using syscall_descriptor_marker = void;

        static constexpr syscall_id id = Id;
        static void                 thunk(u64 a0, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5,
                                          arch::syscall_return&  image,
                                          core::completion<i64>& completion) {
            using function = decltype(&Impl::invoke);
            static_assert(detail::typed_task_handler<function>::value,
                          "async syscall handlers must return core::task<i64> from "
                                          "invoke(typed arguments)");
            const u64 arguments[syscall_argument_count]{a0, a1, a2, a3, a4, a5};
            if (!detail::decoded_arguments_valid<function>::check(arguments)) {
                completion.complete(-EFAULT);
                return;
            }
            auto operation =
                detail::typed_invoker<function>::call(&Impl::invoke, arguments, image);
            if (!operation.valid()) {
                completion.fail(operation.error());
                return;
            }
            operation.resume();
            if (!operation.done()) {
                completion.cancel();
                return;
            }
            if (operation.failed()) {
                completion.fail(operation.error());
                return;
            }
            completion.complete(*operation.value());
        }

        static constexpr syscall_descriptor descriptor{Id, &thunk};
    };

    namespace detail {

        template <typename... Descriptors>
        consteval bool valid_descriptor_ids() {
            constexpr syscall_id
                ids[sizeof...(Descriptors) == 0 ? 1 : sizeof...(Descriptors)] = {
                    Descriptors::id...};
            for (u32 i = 0; i < sizeof...(Descriptors); ++i) {
                if (static_cast<u32>(ids[i]) >= syscall_table_capacity) {
                    return false;
                }
                for (u32 j = i + 1; j < sizeof...(Descriptors); ++j) {
                    if (ids[i] == ids[j]) {
                        return false;
                    }
                }
            }
            return true;
        }

        template <typename Descriptor, typename = void>
        struct valid_descriptor_interface {
            static constexpr bool value = false;
        };

        template <typename Descriptor>
        struct valid_descriptor_interface<
            Descriptor,
            decltype((void) typename Descriptor::syscall_descriptor_marker{},
                     (void) Descriptor::descriptor, (void) Descriptor::descriptor.invoke,
                     (void) &Descriptor::invoke)> {
            static constexpr bool value =
                std::is_same_v<decltype(Descriptor::descriptor.invoke),
                               syscall_handler_fn> &&
                (typed_sync_handler<decltype(&Descriptor::invoke)>::value ||
                 typed_task_handler<decltype(&Descriptor::invoke)>::value);
        };

    } // namespace detail

    namespace detail {

        template <typename Descriptor>
        bool dispatch_descriptor(syscall_id id, u64 a0, u64 a1, u64 a2, u64 a3, u64 a4,
                                 u64 a5, arch::syscall_return& image,
                                 core::completion<i64>& completion) {
            if (Descriptor::descriptor.id != id)
                return false;
            Descriptor::descriptor.invoke(a0, a1, a2, a3, a4, a5, image, completion);
            return true;
        }

        template <typename... Descriptors>
        bool dispatch_descriptors(syscall_id id, u64 a0, u64 a1, u64 a2, u64 a3, u64 a4,
                                  u64 a5, arch::syscall_return& image,
                                  core::completion<i64>& completion) {
            bool dispatched = false;
            ((dispatched =
                  dispatched || dispatch_descriptor<Descriptors>(id, a0, a1, a2, a3, a4,
                                                                 a5, image, completion)),
             ...);
            return dispatched;
        }

    } // namespace detail

    template <typename... Descriptors>
    bool dispatch_async(syscall_id id, u64 a0, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5,
                        arch::syscall_return& image, core::completion<i64>& completion) {
        static_assert(
            detail::valid_descriptor_ids<Descriptors...>(),
            "syscall descriptor IDs must be unique and within ABI table capacity");
        static_assert((detail::valid_descriptor_interface<Descriptors>::value && ...),
                      "syscall descriptors must expose a CRTP handler interface");
        if (detail::dispatch_descriptors<Descriptors...>(id, a0, a1, a2, a3, a4, a5,
                                                         image, completion))
            return true;
        completion.complete(-ENOSYS);
        return false;
    }

    /// Dispatch a system call: the number the caller trapped with, the six
    /// registers it passed, and the return image the exit path will use.
    i64 dispatch(syscall_id id, u64 a0, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5,
                 arch::syscall_return& image);

    bool production_dispatch(syscall_id id, u64 a0, u64 a1, u64 a2, u64 a3, u64 a4,
                             u64 a5, arch::syscall_return& image, i64& result);

} // namespace astia::sys

namespace astia {

    /// The ABI's call number is an unchecked register; map it into the
    /// dispatcher's domain before anything casts it.  Numbers outside the
    /// table are absent calls, not a different syscall.
    template <>
    struct mapper<sys::syscall_id> {
        static optional<sys::syscall_id> from(i64 raw) {
            if (raw < 0 || static_cast<u64>(raw) >= sys::syscall_table_capacity)
                return optional<sys::syscall_id>(error_t::enosys);
            return optional<sys::syscall_id>(
                static_cast<sys::syscall_id>(static_cast<u32>(raw)));
        }
    };

} // namespace astia

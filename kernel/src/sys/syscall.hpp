#pragma once

#include <astia/api/errno.h>
#include <astia/arch/context.hpp>
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
        uname                 = 82,
    };

    /// How many registers the ABI hands every trap — and so the most
    /// arguments any handler can be given.
    inline constexpr u32 syscall_argument_count = 6;

    using syscall_exit_hook = void (*)(arch::syscall_frame& frame, i64 result);

    /// Identity of a kernel-owned syscall operation; not the public syscall ID.
    struct operation_id {
        u64 value;
    };

    /// Opaque metadata carried with a syscall operation; not an ABI argument.
    struct syscall_user_data {
        u64 value;
    };

    struct syscall_context {
        syscall_id                 id;
        i64                        arguments[syscall_argument_count];
        const arch::syscall_frame& frame;
        /// Internal continuation identity, separate from `id`.
        operation_id operation;
        /// Internal continuation metadata, separate from ABI arguments.
        syscall_user_data user_data;
    };

    using async_syscall_handler = void (*)(syscall_context&       context,
                                           core::completion<i64>& completion);
    using task_syscall_handler  = core::task<i64> (*)(syscall_context& context);

    struct syscall_descriptor {
        syscall_id            id;
        async_syscall_handler invoke;
    };

    bool register_exit_hook(syscall_exit_hook hook);

    namespace detail {

        template <typename T>
        struct is_async_handler {
            static constexpr bool value = false;
        };

        template <>
        struct is_async_handler<async_syscall_handler> {
            static constexpr bool value = true;
        };

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
            static bool valid([[maybe_unused]] i64 raw) {
                return true;
            }

            static T decode(i64 raw) {
                return static_cast<T>(raw);
            }
        };

        template <typename T>
        struct is_frame_argument : std::false_type {};

        template <>
        struct is_frame_argument<arch::syscall_frame> : std::true_type {};

        template <typename... Args>
        struct has_frame_prefix : std::false_type {};

        template <typename First, typename... Rest>
        struct has_frame_prefix<First, Rest...>
            : is_frame_argument<std::remove_cvref_t<First>> {};

        template <typename First, typename... Rest>
        inline constexpr usize abi_argument_count =
            sizeof...(Rest) +
            (is_frame_argument<std::remove_cvref_t<First>>::value ? 0 : 1);

        template <typename... Args>
        struct valid_typed_arguments : std::false_type {};

        template <>
        struct valid_typed_arguments<> : std::true_type {};

        template <typename First, typename... Rest>
        struct valid_typed_arguments<First, Rest...>
            : std::bool_constant<
                  !std::is_same_v<std::remove_cvref_t<First>, syscall_context> &&
                  (!is_frame_argument<std::remove_cvref_t<First>>::value ||
                   std::is_same_v<First, const arch::syscall_frame&>) &&
                  ((!std::is_same_v<std::remove_cvref_t<Rest>, syscall_context> &&
                    !is_frame_argument<std::remove_cvref_t<Rest>>::value) &&
                   ...) &&
                  (abi_argument_count<First, Rest...> <= syscall_argument_count)> {};

        template <typename Fn>
        struct typed_sync_handler : std::false_type {};

        template <typename... Args>
        struct typed_sync_handler<i64 (*)(Args...)> : valid_typed_arguments<Args...> {};

        template <typename... Args, usize... I>
        bool valid_arguments(const syscall_context& context, index_sequence<I...>) {
            return (argument_decoder<Args>::valid(context.arguments[I]) && ...);
        }

        template <typename Fn>
        struct frame_invoker;

        template <typename R, typename... Args>
        struct frame_invoker<R (*)(const arch::syscall_frame&, Args...)> {
            static i64 call(R (*function)(const arch::syscall_frame&, Args...),
                            syscall_context& context) {
                const auto indices =
                    typename make_index_sequence<sizeof...(Args)>::type{};
                if (!valid_arguments<Args...>(context, indices))
                    return -EFAULT;
                return call_rest(function, context, indices);
            }

            private:
            template <usize... I>
            static i64 call_rest(R (*function)(const arch::syscall_frame&, Args...),
                                 syscall_context& context, index_sequence<I...>) {
                return static_cast<i64>(
                    function(context.frame,
                             argument_decoder<Args>::decode(context.arguments[I])...));
            }
        };

        template <typename Fn>
        struct typed_invoker;

        template <typename R, typename... Args>
        struct typed_invoker<R (*)(Args...)> {
            static constexpr usize argument_count =
                sizeof...(Args) - (has_frame_prefix<Args...>::value ? 1 : 0);
            static_assert(argument_count <= syscall_argument_count,
                          "syscall handlers may declare at most six ABI arguments");

            static i64 call(R (*function)(Args...), syscall_context& context) {
                if constexpr (has_frame_prefix<Args...>::value) {
                    return frame_invoker<R (*)(Args...)>::call(function, context);
                }
                else {
                    const auto indices =
                        typename make_index_sequence<sizeof...(Args)>::type{};
                    if (!valid_arguments<Args...>(context, indices))
                        return -EFAULT;
                    return call_arguments(function, context, indices);
                }
            }

            private:
            template <usize... I>
            static i64 call_arguments(R (*function)(Args...), syscall_context& context,
                                      index_sequence<I...>) {
                return static_cast<i64>(
                    function(argument_decoder<Args>::decode(context.arguments[I])...));
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
        static void thunk(syscall_context& context, core::completion<i64>& completion) {
            static_assert(
                detail::typed_sync_handler<decltype(&Impl::invoke)>::value,
                "sync syscall handlers must return i64 from invoke(typed arguments); "
                "syscall_context belongs to the dispatcher");
            completion.complete(detail::typed_invoker<decltype(&Impl::invoke)>::call(
                &Impl::invoke, context));
        }

        static constexpr syscall_descriptor descriptor{Id, &thunk};
    };

    template <typename Impl, syscall_id Id>
    struct syscall_handler<Impl, Id, syscall_mode::async> {
        using syscall_descriptor_marker = void;

        static constexpr syscall_id id = Id;
        static void thunk(syscall_context& context, core::completion<i64>& completion) {
            static_assert(std::is_same_v<decltype(&Impl::invoke), task_syscall_handler>,
                          "async syscall handlers must return core::task<i64> from "
                          "invoke(syscall_context&)");
            auto operation = Impl::invoke(context);
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
                is_async_handler<decltype(&Descriptor::invoke)>::value ||
                std::is_same_v<decltype(&Descriptor::invoke), task_syscall_handler> ||
                typed_sync_handler<decltype(&Descriptor::invoke)>::value;
        };

    } // namespace detail

    namespace detail {

        template <typename Descriptor>
        bool dispatch_descriptor(syscall_id id, syscall_context& context,
                                 core::completion<i64>& completion) {
            if (Descriptor::descriptor.id != id)
                return false;
            Descriptor::descriptor.invoke(context, completion);
            return true;
        }

        template <typename... Descriptors>
        bool dispatch_descriptors(syscall_id id, syscall_context& context,
                                  core::completion<i64>& completion) {
            bool dispatched = false;
            ((dispatched = dispatched ||
                           dispatch_descriptor<Descriptors>(id, context, completion)),
             ...);
            return dispatched;
        }

    } // namespace detail

    template <typename... Descriptors>
    bool dispatch_async(syscall_id id, syscall_context& context,
                        core::completion<i64>& completion) {
        static_assert(
            detail::valid_descriptor_ids<Descriptors...>(),
            "syscall descriptor IDs must be unique and within ABI table capacity");
        static_assert((detail::valid_descriptor_interface<Descriptors>::value && ...),
                      "syscall descriptors must expose a CRTP handler interface");
        if (detail::dispatch_descriptors<Descriptors...>(id, context, completion))
            return true;
        completion.complete(-ENOSYS);
        return false;
    }

    /// Dispatch a system call: the number the caller trapped with, the six
    /// registers it passed, and the image the entry packed them into.  The
    /// registers travel as registers, as they did into the entry; the
    /// image travels for the one handler that reads it.
    i64 dispatch(syscall_id id, i64 a0, i64 a1, i64 a2, i64 a3, i64 a4, i64 a5,
                 const arch::syscall_frame& frame);

    bool production_dispatch(syscall_context& context, i64& result);

} // namespace astia::sys
